#include "GarageServer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

namespace
{
	constexpr long long GARAGE_FILE_VERSION = 1;
	constexpr float CLOCK_INTERVAL = 1.f;		// how often everyone is told the time
	constexpr float HELLO_TIMEOUT = 15.f;		// a connection that never says what it wants is dropped

	void Log(const std::string& text)
	{
		auto now = std::chrono::zoned_time(std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
		std::printf("[%s] %s\n", std::format("{:%Y-%m-%d %H:%M:%S}", now).c_str(), text.c_str());
		std::fflush(stdout);
	}
}

GarageServer::Config GarageServer::Config::Load(const std::string& path)
{
	Config config;
	if (!std::filesystem::exists(path))
	{
		std::ofstream file(path);
		file << "# The garage server's settings. Restart the server after changing them.\n";
		file << "name=" << config.name << "\n";
		file << "# Shown in the game's server list, e.g. US East or Europe.\n";
		file << "region=" << config.region << "\n";
		file << "port=" << config.port << "\n";
		file << "max_players=" << config.maxPlayers << "\n";
		file << "garage_file=" << config.garageFile << "\n";
		file << "profiles_file=" << config.profilesFile << "\n";
		file << "# The account server's public identity, which it writes out when it starts: copy that file here.\n";
		file << "account_identity_file=" << config.accountIdentityFile << "\n";
		file << "# The server's private identity key: keep it secret, and keep it. Players' games refuse a\n";
		file << "# server whose identity changes.\n";
		file << "identity_file=" << config.identityFile << "\n";
		file << "# Real minutes for a full 24 hours; 0 stops the sun.\n";
		file << "day_length_minutes=" << config.dayLengthMinutes << "\n";
		file << "autosave_seconds=" << config.autosaveSeconds << "\n";
		return config;
	}

	std::ifstream file(path);
	std::string line;
	while (std::getline(file, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty() || line[0] == '#')
			continue;
		auto equals = line.find('=');
		if (equals == std::string::npos)
			continue;
		std::string key = line.substr(0, equals);
		std::string text = line.substr(equals + 1);
		std::stringstream value(text);
		if (key == "name") config.name = text;
		else if (key == "region") config.region = text;
		else if (key == "port") value >> config.port;
		else if (key == "max_players") value >> config.maxPlayers;
		else if (key == "garage_file") config.garageFile = text;
		else if (key == "profiles_file") config.profilesFile = text;
		else if (key == "account_identity_file") config.accountIdentityFile = text;
		else if (key == "identity_file") config.identityFile = text;
		else if (key == "day_length_minutes") value >> config.dayLengthMinutes;
		else if (key == "autosave_seconds") value >> config.autosaveSeconds;
	}
	config.port = std::clamp(config.port, 1024, 65535);
	config.maxPlayers = std::clamp(config.maxPlayers, 1, NetProtocol::MAX_SLOTS);
	config.dayLengthMinutes = (std::max)(config.dayLengthMinutes, 0.f);
	config.autosaveSeconds = std::clamp(config.autosaveSeconds, 5.f, 3600.f);
	return config;
}

bool GarageServer::Start(const Config& config, std::string& error)
{
	m_config = config;
	if (!Load(error) || !m_profiles.Load(config.profilesFile, error))
		return false;
	// Who logs players in: tickets are checked against the account server's identity.
	{
		std::ifstream file(config.accountIdentityFile);
		std::getline(file, m_accountIdentity);
		while (!m_accountIdentity.empty() && std::isspace(static_cast<unsigned char>(m_accountIdentity.back())))
			m_accountIdentity.pop_back();
		if (m_accountIdentity.empty())
		{
			error = std::format("There's no {} here: copy it from the account server, which writes it out when it starts.",
				config.accountIdentityFile);
			return false;
		}
	}
	m_identity = Net::Identity::LoadOrCreate(config.identityFile, error);
	if (!m_identity)
	{
		error = std::format("The server's identity: {}.", error);
		return false;
	}
	m_listener = Net::Listener::Listen(static_cast<uint16_t>(config.port), error);
	if (!m_listener)
	{
		error = std::format("Couldn't listen on port {}: {}.", config.port, error);
		return false;
	}
	Log(std::format("\"{}\" ({}) is open on port {}, for up to {} players.", config.name,
		config.region.empty() ? "no region" : config.region, config.port, config.maxPlayers));
	Log(m_records.empty() ? std::format("{} is new: the first player to join starts it.", config.garageFile)
		: std::format("Loaded {} records from {}.", m_records.size(), config.garageFile));
	Log(std::format("Log ins are checked against the account server with fingerprint {}.", Net::Fingerprint(m_accountIdentity)));
	// Players can check this against what their game remembers for this server.
	Log(std::format("Identity fingerprint: {}", Net::Fingerprint(m_identity->PublicKey())));
	return true;
}

void GarageServer::Stop()
{
	if (SaveAll())
		Log("Garage and profiles saved.");
	for (auto& peer : m_peers)
		if (peer.connection)
			peer.connection->Flush();
	m_peers.clear();
	m_listener.reset();
}

void GarageServer::Update(float deltaTime)
{
	while (auto connection = m_listener->Accept())
		m_peers.push_back({ std::move(connection) });

	for (size_t i = 0; i < m_peers.size();)
	{
		auto& peer = m_peers[i];
		bool open = true;
		peer.age += deltaTime;
		Receive(peer, open);
		if (peer.state == PeerState::NEW && peer.age > HELLO_TIMEOUT)
			open = false;
		if (open)
		{
			i++;
			continue;
		}
		Peer gone = std::move(peer);
		m_peers.erase(m_peers.begin() + static_cast<std::ptrdiff_t>(i));
		Departed(gone);
	}

	// The clock.
	if (m_config.dayLengthMinutes > 0.f)
		m_timeOfDay = std::fmod(m_timeOfDay + deltaTime * 24.f / (m_config.dayLengthMinutes * 60.f), 24.f);
	m_clockTimer += deltaTime;
	if (m_clockTimer >= CLOCK_INTERVAL)
	{
		m_clockTimer = 0.f;
		SendToPlaying(Writer().Word("clock").Num(m_timeOfDay).Num(m_config.dayLengthMinutes).Text());
	}

	for (auto& peer : m_peers)
		peer.connection->Flush();
	AutoSave(deltaTime);
}

void GarageServer::Receive(Peer& peer, bool& open)
{
	std::vector<std::string> messages;
	open = peer.connection->Receive(messages);
	for (const auto& message : messages)
	{
		Reader in(message);
		std::string type = in.Next();
		if (type == "query")
		{
			// For the server list: what this is and how full. The game times the answer for its ping.
			peer.connection->Send(Writer().Word("info").Int(NetProtocol::VERSION).Str(m_config.name).Str(m_config.region)
				.Int(PlayerCount()).Int(m_config.maxPlayers).Text());
			peer.connection->Flush();
		}
		else if (type == "secure" && !peer.connection->IsEncrypted())
		{
			Secure(peer, in, open);
			if (!open)
				break;
		}
		else if (type == "hello" && peer.state == PeerState::NEW)
		{
			// Never a log in in the clear: a game that sends one is too old to make the connection safe.
			if (!peer.connection->IsEncrypted())
			{
				Refuse(peer, "That garage is running a different version of the game.");
				open = false;
				break;
			}
			Hello(peer, in, open);
			if (!open)
				break;
		}
		else if (peer.state != PeerState::PLAYING)
			continue;
		else if (type == "ops")
		{
			NetProtocol::Batch batch;
			if (NetProtocol::ReadOps(message, batch))
				ApplyOps(peer.id, batch.sequence, std::move(batch.ops));
		}
		else if (type == "presence")
		{
			// Passed on to everyone else, as long as it's who it says it is. Where they are is kept
			// in their profile here, for when they come back.
			if (in.Int() != peer.id)
				continue;
			SendToPlaying(message, peer.id);
			in.Next();
			float x = static_cast<float>(in.Num());
			float y = static_cast<float>(in.Num());
			float z = static_cast<float>(in.Num());
			float yaw = static_cast<float>(in.Num());
			float pitch = static_cast<float>(in.Num());
			if (!in.Failed())
			{
				auto& profile = *peer.profile;
				profile.placed = true;
				profile.x = x;
				profile.y = y;
				profile.z = z;
				profile.yaw = yaw;
				profile.pitch = pitch;
				m_profiles.MarkDirty();
			}
		}
		else if (type == "profile")
		{
			std::string layout = in.Next();
			if (!in.Failed() && layout != peer.profile->layout)
			{
				peer.profile->layout = layout;
				m_profiles.MarkDirty();
			}
		}
	}
}

void GarageServer::Secure(Peer& peer, Reader& in, bool& open)
{
	long long version = in.Int();
	std::string clientKey = in.Next();
	if (in.Failed() || version != NetProtocol::VERSION)
	{
		Refuse(peer, "That garage is running a different version of the game.");
		open = false;
		return;
	}
	auto keyExchange = Net::KeyExchange::Create();
	std::unique_ptr<Net::Cipher> sending, receiving;
	const std::string serverKey = keyExchange ? keyExchange->PublicKey() : "";
	if (!keyExchange || !keyExchange->Derive(clientKey, true, clientKey, serverKey, sending, receiving))
	{
		Refuse(peer, "The garage couldn't make a safe connection.");
		open = false;
		return;
	}
	// Our key, and our identity's signature of both keys: proof to the game that it's us at this
	// end. Sent in the clear, as the last thing that is.
	peer.connection->Send(Writer().Word("secure").Word(serverKey).Word(m_identity->PublicKey())
		.Word(m_identity->Sign(Net::HandshakeTranscript(clientKey, serverKey))).Text());
	peer.connection->Encrypt(std::move(sending), std::move(receiving));
}

void GarageServer::Refuse(Peer& peer, const std::string& why)
{
	Log(std::format("Turned away {}: {}", peer.connection->GetAddress(), why));
	peer.connection->Send(Writer().Word("reject").Str(why).Text());
	peer.connection->Flush();
}

void GarageServer::Hello(Peer& peer, Reader& in, bool& open)
{
	// Who they are is the account server's word: its ticket, signed with its identity key.
	long long version = in.Int();
	std::string ticketText = in.Next();
	std::string signature = in.Next();
	std::optional<NetProtocol::Ticket> ticket;
	std::string refusal;
	int slot = FreeSlot();
	if (in.Failed() || version != NetProtocol::VERSION)
		refusal = "That garage is running a different version of the game.";
	else if (!Net::VerifySignature(m_accountIdentity, ticketText, signature) || !(ticket = NetProtocol::ReadTicket(ticketText)))
		refusal = "That garage doesn't use the account server you logged in to.";
	else if (NetProtocol::TicketExpired(*ticket))
		refusal = "Your log in has run out: log in again.";
	else if (IsLoggedIn(ticket->account))
		refusal = std::format("{} is already in the garage.", ticket->username);
	else if (slot == 0)
		refusal = std::format("That garage is full ({} players).", m_config.maxPlayers);
	if (!refusal.empty())
	{
		Refuse(peer, refusal);
		open = false;
		return;
	}
	peer.profile = &m_profiles.Get(ticket->account, ticket->username);
	peer.id = ticket->account;
	peer.slot = slot;
	peer.name = ticket->username;
	Log(std::format("{} (account {}) joined from {}.", peer.name, peer.id, peer.connection->GetAddress()));

	// A brand new garage: the first player's fresh shop becomes it, and anyone else waits for that.
	if (m_records.empty())
	{
		if (m_seeder == 0)
		{
			m_seeder = peer.id;
			Welcome(peer, true);
		}
		else
			peer.state = PeerState::WAITING;
		return;
	}
	Welcome(peer, false);
}

void GarageServer::Welcome(Peer& peer, bool seed)
{
	Writer welcome;
	welcome.Word("welcome").Int(peer.id).Int(peer.slot).Int(seed).Num(m_timeOfDay).Num(m_config.dayLengthMinutes);
	std::vector<const Peer*> others;
	for (const auto& other : m_peers)
		if (other.state == PeerState::PLAYING && other.id != peer.id)
			others.push_back(&other);
	welcome.Int(others.size());
	for (const auto* other : others)
		welcome.Int(other->id).Str(other->name);
	welcome.Int(m_records.size());
	for (const auto& [key, text] : m_records)
		welcome.Str(key).Str(text);
	// Their backpack as they left it, and where they stood.
	const auto& profile = *peer.profile;
	welcome.Str(profile.layout).Int(profile.placed).Num(profile.x).Num(profile.y).Num(profile.z).Num(profile.yaw).Num(profile.pitch);
	peer.connection->Send(welcome.Text());
	peer.state = PeerState::PLAYING;
}

void GarageServer::ApplyOps(int author, long long sequence, std::vector<NetProtocol::Op> ops)
{
	using NetProtocol::OpKind;
	for (auto& op : ops)
	{
		// Money and the tallies go up or down by what the player's did, from wherever they are now.
		if (op.kind == OpKind::ADD)
		{
			auto current = m_records.find(op.key);
			double base = current != m_records.end() ? NetProtocol::CounterValue(current->second) : 0.0;
			op = { OpKind::SET, op.key, NetProtocol::CounterText(base + NetProtocol::CounterValue(op.text)) };
		}
		if (op.kind == OpKind::ERASE)
			m_records.erase(op.key);
		else
			m_records[op.key] = op.text;
	}
	if (ops.empty())
		return;
	m_dirty = true;
	// To everyone, the author too: that's how they know the garage has it.
	SendToPlaying(NetProtocol::OpsMessage(author, sequence, ops));
	if (author == m_seeder && !m_records.empty())
	{
		Log("The new garage has its first shop.");
		m_seeder = 0;
		WelcomeWaiting();
	}
}

void GarageServer::Departed(const Peer& peer)
{
	if (peer.state == PeerState::NEW)
		return;
	Log(std::format("{} (account {}) left.", peer.name, peer.id));
	if (peer.state == PeerState::PLAYING)
	{
		SendToPlaying(Writer().Word("left").Int(peer.id).Text());

		// A delivery in their arms is put down. Their backpack stays theirs, out of everyone's reach,
		// till they're back.
		std::vector<NetProtocol::Op> ops;
		for (const auto& [key, text] : m_records)
			if (auto dropped = NetProtocol::DroppedPackage(key, text, peer.id))
				ops.push_back({ NetProtocol::OpKind::SET, key, *dropped });
		ApplyOps(NetProtocol::SERVER_AUTHOR, 0, std::move(ops));
	}
	if (peer.id == m_seeder)
	{
		m_seeder = 0;
		WelcomeWaiting();
	}
	if (PlayerCount() == 0)
		SaveAll();
}

bool GarageServer::SaveAll()
{
	std::string error;
	bool saved = true;
	if (m_dirty && !Save(error))
	{
		Log("Couldn't save the garage: " + error);
		saved = false;
	}
	if (m_profiles.IsDirty() && !m_profiles.Save(error))
	{
		Log("Couldn't save the profiles: " + error);
		saved = false;
	}
	return saved;
}

void GarageServer::WelcomeWaiting()
{
	for (auto& peer : m_peers)
	{
		if (peer.state != PeerState::WAITING)
			continue;
		if (m_records.empty())
		{
			if (m_seeder != 0)
				return;
			m_seeder = peer.id;
			Welcome(peer, true);
		}
		else
			Welcome(peer, false);
	}
}

void GarageServer::SendToPlaying(const std::string& message, int except)
{
	for (auto& peer : m_peers)
		if (peer.state == PeerState::PLAYING && peer.id != except)
			peer.connection->Send(message);
}

int GarageServer::PlayerCount() const
{
	return static_cast<int>(std::count_if(m_peers.begin(), m_peers.end(), [](const Peer& peer) { return peer.state != PeerState::NEW; }));
}

int GarageServer::FreeSlot() const
{
	for (int slot = 1; slot <= m_config.maxPlayers; slot++)
		if (std::none_of(m_peers.begin(), m_peers.end(), [&](const Peer& peer) { return peer.state != PeerState::NEW && peer.slot == slot; }))
			return slot;
	return 0;
}

bool GarageServer::IsLoggedIn(int account) const
{
	return std::any_of(m_peers.begin(), m_peers.end(), [&](const Peer& peer) { return peer.state != PeerState::NEW && peer.id == account; });
}

bool GarageServer::Load(std::string& error)
{
	m_records.clear();
	if (!std::filesystem::exists(m_config.garageFile))
		return true;
	std::ifstream file(m_config.garageFile, std::ios::binary);
	std::stringstream text;
	text << file.rdbuf();
	Reader in(text.str());
	in.Expect("lvgarage");
	if (in.Int() != GARAGE_FILE_VERSION)
		in.Fail("it was saved by a different version of the server");
	in.Expect("time");
	m_timeOfDay = static_cast<float>(in.Num());
	in.Expect("records");
	long long count = in.Int();
	for (long long i = 0; i < count && !in.Failed(); i++)
	{
		std::string key = in.Next();
		m_records[key] = in.Next();
	}
	if (in.Failed())
	{
		error = std::format("{} couldn't be read: {}.", m_config.garageFile, in.Error());
		return false;
	}
	m_timeOfDay = std::fmod(std::abs(m_timeOfDay), 24.f);

	// Nobody's here yet: a delivery anyone was carrying when the server last stopped is put down.
	for (auto& [key, text] : m_records)
		if (auto dropped = NetProtocol::DroppedPackage(key, text, std::nullopt))
			text = *dropped;
	return true;
}

bool GarageServer::Save(std::string& error)
{
	if (m_records.empty())
		return true;
	Writer out;
	out.Word("lvgarage").Int(GARAGE_FILE_VERSION).Line();
	out.Word("time").Num(m_timeOfDay).Line();
	out.Word("records").Int(m_records.size()).Line();
	for (const auto& [key, text] : m_records)
		out.Str(key).Str(text).Line();

	// Written beside the old one, then swapped in, so a crash mid-save never loses the garage.
	std::string temporary = m_config.garageFile + ".tmp";
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		file << out.Text();
		if (!file)
		{
			error = std::format("couldn't write {}", temporary);
			return false;
		}
	}
	std::error_code fileError;
	std::filesystem::rename(temporary, m_config.garageFile, fileError);
	if (fileError)
	{
		error = fileError.message();
		return false;
	}
	m_dirty = false;
	return true;
}

void GarageServer::AutoSave(float deltaTime)
{
	m_saveTimer += deltaTime;
	if (m_saveTimer < m_config.autosaveSeconds)
		return;
	m_saveTimer = 0.f;
	SaveAll();
}
