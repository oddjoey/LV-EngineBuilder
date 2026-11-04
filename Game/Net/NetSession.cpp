#include "NetSession.h"

#include <algorithm>
#include <format>

#include <imgui.h>

#include "LV/Engine/Objects/ObjectManager.h"
#include "../Save/SaveText.h"
#include "../Simulation/Workshop.h"
#include "../World/FirstPersonController.h"
#include "../World/ShopWorld.h"
#include "../World/ShopWorldShared.h"

namespace
{
	constexpr float SYNC_INTERVAL = 0.1f;		// how often changes to the garage are sent
	constexpr float PRESENCE_INTERVAL = 0.05f;	// and where this player is
	constexpr float PROFILE_INTERVAL = 1.f;		// and how their backpack is laid out
	constexpr float CONNECT_TIMEOUT = 6.f;
	// Each server's identity, from the first time it was joined.
	constexpr const char* KNOWN_SERVERS_FILE = "known_servers.txt";
	constexpr float EYE_HEIGHT = 1.68f;

	// Each player's colour: their overalls and their name. Hues spread round the wheel, so however
	// many are in the garage, neighbours in the list never look alike.
	glm::vec4 PlayerColor(int id)
	{
		float hue = std::fmod(0.08f + id * 0.618034f, 1.f);
		float r = 0.f, g = 0.f, b = 0.f;
		ImGui::ColorConvertHSVtoRGB(hue, 0.6f, 0.95f, r, g, b);
		return { r, g, b, 1.f };
	}

	glm::quat ViewRotation(float yaw, float pitch)
	{
		return glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f)) * glm::angleAxis(pitch, glm::vec3(1.f, 0.f, 0.f));
	}
}

bool NetSession::Join(const std::string& address, uint16_t port, const std::string& username, const std::string& password,
	bool newAccount, std::string& error)
{
	if (m_role != Role::OFFLINE)
	{
		error = "Already in a garage.";
		return false;
	}
	m_keyExchange = Net::KeyExchange::Create();
	if (!m_keyExchange)
	{
		error = "Windows cryptography didn't start, so the connection can't be made safe.";
		return false;
	}
	m_connection = Net::Connection::Begin(address, port, error);
	if (!m_connection)
	{
		error = std::format("Couldn't reach {}: {}.", address, error);
		return false;
	}
	// Keys first: the log in only goes once the connection is encrypted.
	m_connection->Send(Writer().Word("secure").Int(NetProtocol::VERSION).Word(m_keyExchange->PublicKey()).Text());
	m_password = password;
	m_newAccount = newAccount;

	m_role = Role::CONNECTING;
	m_server = std::format("{}:{}", address, port);
	m_localName = username;
	m_localId = -1;
	m_connectTime = 0.f;
	m_welcomeReady = false;
	m_seed = false;
	m_disconnected = false;
	m_pending.clear();
	m_incoming.clear();
	m_nextSequence = 1;
	return true;
}

void NetSession::Leave(ShopWorld& world)
{
	std::vector<int> ids;
	for (const auto& [id, remote] : m_remotes)
		ids.push_back(id);
	for (int id : ids)
		RemoveRemote(id, world);
	// The backpack as it is now goes to the account on the way out.
	if (m_connection && m_role == Role::PLAYING)
	{
		std::string layout = NetState::CaptureLayout(world);
		if (layout != m_sentLayout)
			m_connection->Send(Writer().Word("profile").Str(layout).Text());
	}
	if (m_connection)
		m_connection->Flush();
	m_connection.reset();
	m_keyExchange.reset();
	std::fill(m_password.begin(), m_password.end(), '\0');
	m_password.clear();
	m_role = Role::OFFLINE;
	m_localId = 0;
	m_shadow.clear();
	m_incoming.clear();
	m_pending.clear();
	m_welcomeReady = false;
	m_welcomeRecords.clear();
}

void NetSession::Disconnect(ShopWorld& world, const std::string& why)
{
	Notice(why);
	Leave(world);
	m_disconnected = true;
}

std::vector<NetSession::PlayerInfo> NetSession::GetPlayers() const
{
	std::vector<PlayerInfo> players;
	if (m_role != Role::PLAYING)
		return players;
	players.push_back({ m_localId, m_localName, true });
	for (const auto& [id, remote] : m_remotes)
		players.push_back({ id, remote.name, false });
	std::sort(players.begin(), players.end(), [](const PlayerInfo& a, const PlayerInfo& b) { return a.id < b.id; });
	return players;
}

void NetSession::Update(float deltaTime, Workshop& workshop, ShopWorld& world, const FirstPersonController& player, float& timeOfDay)
{
	if (m_role == Role::OFFLINE)
		return;

	if (m_role == Role::CONNECTING)
	{
		std::string error;
		auto status = m_connection->PollConnect(error);
		m_connectTime += deltaTime;
		if (status == Net::Connection::Status::FAILED)
		{
			Disconnect(world, std::format("Couldn't join {}: {}.", m_server, error));
			return;
		}
		if (status == Net::Connection::Status::CONNECTING)
		{
			if (m_connectTime > CONNECT_TIMEOUT)
				Disconnect(world, std::format("Couldn't join {}: it timed out.", m_server));
			return;
		}
		m_role = Role::JOINING;
	}

	Receive(world, timeOfDay);
	if (m_role != Role::PLAYING)
		return;

	m_syncTimer += deltaTime;
	m_presenceTimer += deltaTime;
	if (m_syncTimer >= SYNC_INTERVAL)
	{
		m_syncTimer = 0.f;
		Sync(workshop, world);
	}
	if (m_presenceTimer >= PRESENCE_INTERVAL)
	{
		m_presenceTimer = 0.f;
		m_connection->Send(PresenceMessage(player));
	}
	// The backpack's layout, kept with the account whenever it changes.
	m_profileTimer += deltaTime;
	if (m_profileTimer >= PROFILE_INTERVAL)
	{
		m_profileTimer = 0.f;
		if (std::string layout = NetState::CaptureLayout(world); layout != m_sentLayout)
		{
			m_connection->Send(Writer().Word("profile").Str(layout).Text());
			m_sentLayout = std::move(layout);
		}
	}
	m_connection->Flush();
	MoveAvatars(deltaTime, world);
}

void NetSession::Receive(ShopWorld& world, float& timeOfDay)
{
	std::vector<std::string> messages;
	bool open = m_connection->Receive(messages);
	for (const auto& message : messages)
	{
		Reader in(message);
		std::string type = in.Next();
		if (!m_connection->IsEncrypted())
		{
			// Nothing but the handshake (or being turned away) before the connection is safe.
			if (type == "secure")
			{
				if (!Secure(in, world))
					return;
			}
			else if (type == "reject")
			{
				Disconnect(world, in.Next());
				return;
			}
			continue;
		}
		if (type == "welcome")
		{
			m_localId = static_cast<int>(in.Int());
			m_slot = static_cast<int>(in.Int());
			m_seed = in.Int() != 0;
			float time = static_cast<float>(in.Num());
			m_dayLengthMinutes = static_cast<float>(in.Num());
			size_t players = in.Count();
			for (size_t i = 0; i < players && !in.Failed(); i++)
			{
				int id = static_cast<int>(in.Int());
				m_remotes[id].name = in.Next();
			}
			m_welcomeRecords.clear();
			size_t count = in.Count();
			for (size_t i = 0; i < count && !in.Failed(); i++)
			{
				std::string key = in.Next();
				m_welcomeRecords[key] = in.Next();
			}
			m_welcomeLayout = in.Next();
			m_welcomePlaced = in.Int() != 0;
			m_welcomeFeet.x = static_cast<float>(in.Num());
			m_welcomeFeet.y = static_cast<float>(in.Num());
			m_welcomeFeet.z = static_cast<float>(in.Num());
			m_welcomeYaw = static_cast<float>(in.Num());
			m_welcomePitch = static_cast<float>(in.Num());
			if (in.Failed())
			{
				Disconnect(world, "The garage didn't arrive whole.");
				return;
			}
			timeOfDay = time;
			m_welcomeReady = true;
		}
		else if (type == "reject")
		{
			Disconnect(world, in.Next());
			return;
		}
		else if (type == "ops")
		{
			Batch batch;
			if (NetProtocol::ReadOps(message, batch))
				m_incoming.push_back(std::move(batch));
		}
		else if (type == "presence" && m_role == Role::PLAYING)
			ReadPresence(message, world);
		else if (type == "clock")
		{
			float time = static_cast<float>(in.Num());
			float dayLength = static_cast<float>(in.Num());
			if (!in.Failed())
			{
				m_dayLengthMinutes = dayLength;
				SetClock(time, timeOfDay);
			}
		}
		else if (type == "left")
		{
			// What they carried is put down by the server, in a change that follows this.
			int id = static_cast<int>(in.Int());
			if (auto remote = m_remotes.find(id); remote != m_remotes.end())
				Notice(std::format("{} left the garage.", remote->second.name));
			RemoveRemote(id, world);
		}
	}
	if (!open)
		Disconnect(world, m_role == Role::PLAYING ? "Lost the connection to the garage server." : "The garage server closed the connection.");
}

bool NetSession::Secure(Reader& in, ShopWorld& world)
{
	std::string serverKey = in.Next();
	std::string identity = in.Next();
	std::string signature = in.Next();
	const std::string& clientKey = m_keyExchange->PublicKey();

	// The server proves it holds its identity key by signing both sides' keys: someone in between
	// swapping keys can't.
	if (in.Failed() || !Net::VerifySignature(identity, Net::HandshakeTranscript(clientKey, serverKey), signature))
	{
		Disconnect(world, std::format("{} couldn't prove who it is, so you weren't logged in.", m_server));
		return false;
	}
	// And it's the server that was at this address before.
	std::string fingerprint = Net::Fingerprint(identity);
	if (Net::KnownHosts(KNOWN_SERVERS_FILE).Check(m_server, fingerprint) == Net::KnownHosts::Result::CHANGED)
	{
		Disconnect(world, std::format("{} isn't the server that was there before: its identity has changed, so you weren't "
			"logged in. If its owner changed it on purpose, delete its line from {} and join again.", m_server, KNOWN_SERVERS_FILE));
		return false;
	}

	std::unique_ptr<Net::Cipher> sending, receiving;
	if (!m_keyExchange->Derive(serverKey, false, clientKey, serverKey, sending, receiving))
	{
		Disconnect(world, std::format("Couldn't make a safe connection to {}.", m_server));
		return false;
	}
	m_connection->Encrypt(std::move(sending), std::move(receiving));
	m_keyExchange.reset();

	// Safe now: the log in, and the password forgotten here.
	m_connection->Send(Writer().Word("hello").Int(NetProtocol::VERSION).Int(m_newAccount).Str(m_localName).Str(m_password).Text());
	std::fill(m_password.begin(), m_password.end(), '\0');
	m_password.clear();
	return true;
}

void NetSession::Sync(Workshop& workshop, ShopWorld& world)
{
	// Ours go first, so what arrived meanwhile is applied on top of what we'd done, not instead of it.
	auto now = NetState::Capture(workshop, world, m_localId);
	auto ops = Diff(now);
	m_shadow = std::move(now);
	if (!ops.empty())
	{
		long long sequence = m_nextSequence++;
		for (const auto& op : ops)
			m_pending[op.key] = sequence;
		m_connection->Send(NetProtocol::OpsMessage(m_localId, sequence, ops));
	}

	std::vector<StateChange> changes;
	for (const auto& batch : m_incoming)
		for (const auto& op : batch.ops)
		{
			if (auto pending = m_pending.find(op.key); pending != m_pending.end())
			{
				// Our own change come back: from here on the server's record is ours. Anyone else's
				// is older than ours, which the server will pass on after it.
				if (batch.author != m_localId || batch.sequence != pending->second)
					continue;
				m_pending.erase(pending);
			}
			if (op.kind == NetProtocol::OpKind::ERASE)
			{
				m_shadow.erase(op.key);
				changes.push_back({ op.key, std::nullopt });
			}
			else
			{
				m_shadow[op.key] = op.text;
				changes.push_back({ op.key, op.text });
			}
		}
	m_incoming.clear();
	if (!changes.empty())
		NetState::Apply(workshop, world, changes, m_localId);
}

bool NetSession::TakeWelcome()
{
	bool ready = m_welcomeReady;
	m_welcomeReady = false;
	return ready;
}

void NetSession::ApplyWelcome(Workshop& workshop, ShopWorld& world, FirstPersonController& player)
{
	// A new garage starts as this player's fresh shop: nothing is applied, and the first sync sends
	// all of it.
	if (!m_seed)
	{
		std::vector<StateChange> changes;
		for (const auto& [key, text] : m_welcomeRecords)
			changes.push_back({ key, text });
		NetState::Apply(workshop, world, changes, m_localId, true);
	}
	NetState::ClaimIds(workshop, world, m_slot);
	// Back with what they had, laid out as they left it, and standing where they were.
	NetState::RestoreLayout(world, m_welcomeLayout);
	m_sentLayout = m_welcomeLayout;
	if (m_welcomePlaced)
	{
		player.SetPosition(m_welcomeFeet);
		player.SetYaw(m_welcomeYaw);
		player.SetPitch(m_welcomePitch);
	}
	m_shadow = std::move(m_welcomeRecords);
	m_welcomeRecords.clear();
	m_role = Role::PLAYING;
	m_syncTimer = 0.f;
	m_presenceTimer = 0.f;
	m_profileTimer = 0.f;
	Notice(m_seed ? std::format("Welcome, {}. This is a brand new garage: it starts as a fresh shop.", m_localName)
		: std::format("Welcome {}, {}.", m_welcomePlaced ? "back" : "to the garage", m_localName));
}

bool NetSession::TakeDisconnected()
{
	bool disconnected = m_disconnected;
	m_disconnected = false;
	return disconnected;
}

std::vector<std::string> NetSession::TakeNotices()
{
	return std::exchange(m_notices, {});
}

std::vector<NetSession::Op> NetSession::Diff(const StateRecords& now) const
{
	using NetProtocol::OpKind;
	std::vector<Op> ops;
	for (const auto& [key, text] : now)
	{
		auto before = m_shadow.find(key);
		if (before != m_shadow.end() && before->second == text)
			continue;
		if (NetProtocol::IsCounter(key))
		{
			double delta = NetProtocol::CounterValue(text) - (before != m_shadow.end() ? NetProtocol::CounterValue(before->second) : 0.0);
			if (delta != 0.0)
				ops.push_back({ OpKind::ADD, key, NetProtocol::CounterText(delta) });
			continue;
		}
		ops.push_back({ OpKind::SET, key, text });
	}
	for (const auto& [key, text] : m_shadow)
		if (!now.contains(key))
			ops.push_back({ OpKind::ERASE, key, "" });
	return ops;
}

std::string NetSession::PresenceMessage(const FirstPersonController& player) const
{
	glm::vec3 feet = player.GetFeetPosition();
	Writer out;
	out.Word("presence").Int(m_localId).Str(m_localName).Num(feet.x).Num(feet.y).Num(feet.z).Num(player.GetYaw()).Num(player.GetPitch());
	return out.Text();
}

void NetSession::ReadPresence(const std::string& message, ShopWorld& world)
{
	Reader in(message);
	in.Expect("presence");
	int id = static_cast<int>(in.Int());
	std::string name = in.Next();
	float x = static_cast<float>(in.Num());
	float y = static_cast<float>(in.Num());
	float z = static_cast<float>(in.Num());
	float yaw = static_cast<float>(in.Num());
	float pitch = static_cast<float>(in.Num());
	if (in.Failed() || id == m_localId || id < 1)
		return;

	auto& remote = m_remotes[id];
	if (remote.name.empty())
		Notice(std::format("{} joined the garage.", name));
	remote.name = name;
	remote.feet = { x, y, z };
	remote.yaw = yaw;
	remote.pitch = pitch;
	if (!remote.avatar)
	{
		remote.avatar = world.Spawn(ShopModels::PLAYER, "", remote.feet, yaw);
		remote.avatar->SetTint(PlayerColor(id));
		remote.avatar->SetPickable(false);
	}
}

void NetSession::SetClock(float time, float& timeOfDay)
{
	// The sun is in the same place for everyone. Small differences are left alone: they come from
	// the time the message took, and snapping to them would make the shadows twitch.
	if (std::abs(std::remainder(time - timeOfDay, 24.f)) > 0.02f)
		timeOfDay = time;
}

void NetSession::RemoveRemote(int id, ShopWorld& world)
{
	auto it = m_remotes.find(id);
	if (it == m_remotes.end())
		return;
	if (it->second.avatar)
		ObjectManager::Get()->RemoveObject(it->second.avatar);
	m_remotes.erase(it);
	world.m_remoteHands.erase(id);
}

void NetSession::MoveAvatars(float deltaTime, ShopWorld& world)
{
	float ease = 1.f - std::exp(-12.f * deltaTime);
	for (auto& [id, remote] : m_remotes)
	{
		if (!remote.avatar)
			continue;
		if (!remote.placed)
		{
			remote.shownFeet = remote.feet;
			remote.shownYaw = remote.yaw;
			remote.placed = true;
		}
		remote.shownFeet += (remote.feet - remote.shownFeet) * ease;
		float turn = std::remainder(remote.yaw - remote.shownYaw, glm::two_pi<float>());
		remote.shownYaw += turn * ease;
		remote.avatar->SetPosition(remote.shownFeet);
		remote.avatar->SetRotation(ShopLayout::Yaw(remote.shownYaw));

		// What they hold is held out in front of them, a little to the right.
		glm::quat view = ViewRotation(remote.shownYaw, remote.pitch);
		glm::vec3 eye = remote.shownFeet + glm::vec3(0.f, EYE_HEIGHT, 0.f);
		glm::vec3 hand = eye + view * glm::vec3(0.22f, -0.3f, -0.5f);
		world.m_remoteHands[id] = { hand, remote.shownYaw };
	}
}

void NetSession::DrawNameTags(const ShopWorld& world, const FirstPersonController& player) const
{
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	glm::vec3 eye = player.GetEyePosition();
	for (const auto& [id, remote] : m_remotes)
	{
		if (!remote.avatar || !remote.placed)
			continue;
		glm::vec3 head = remote.shownFeet + glm::vec3(0.f, 2.05f, 0.f);
		float distance = glm::length(head - eye);
		if (distance > 30.f || glm::dot(head - eye, player.GetForward()) <= 0.f)
			continue;
		auto screen = world.ProjectToScreen(head, player);
		if (!screen)
			continue;
		ImVec2 size = ImGui::CalcTextSize(remote.name.c_str());
		ImVec2 at = { screen->x - size.x * 0.5f, screen->y - size.y };
		glm::vec4 color = PlayerColor(id);
		drawList->AddRectFilled({ at.x - 5.f, at.y - 2.f }, { at.x + size.x + 5.f, at.y + size.y + 2.f }, IM_COL32(0, 0, 0, 140), 4.f);
		drawList->AddText(at, ImGui::ColorConvertFloat4ToU32({ color.r, color.g, color.b, 1.f }), remote.name.c_str());
	}
}
