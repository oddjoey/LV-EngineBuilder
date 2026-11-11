#include "GarageServer.h"

#include "../Game/LocalTime.h"

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
	constexpr long long GARAGE_FILE_VERSION = 2;		// 2: several garages, each with its members
	constexpr float CLOCK_INTERVAL = 1.f;		// how often everyone is told the time
	constexpr float HELLO_TIMEOUT = 15.f;		// a connection that never says what it wants is dropped
	constexpr float PREFIXES_INTERVAL = 3.f;		// how often the prefixes file is looked at
	constexpr size_t PREFIX_MAX = 24;

	void Log(const std::string& text)
	{
		std::printf("[%s] %s\n", LocalTimeText(std::chrono::system_clock::now(), "%Y-%m-%d %H:%M:%S").c_str(), text.c_str());
		std::fflush(stdout);
	}

	// "[Owner] joey", for what everyone reads.
	std::string FullName(const std::string& prefix, const std::string& name)
	{
		return prefix.empty() ? name : prefix + " " + name;
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
		file << "# Prefixes in front of players' names, like [Owner] or [VIP]: see that file.\n";
		file << "prefixes_file=" << config.prefixesFile << "\n";
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
		else if (key == "prefixes_file") config.prefixesFile = text;
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
	LoadPrefixes();
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
	Log(m_garages.empty() ? std::format("{} is new: the first player to join makes the first garage.", config.garageFile)
		: std::format("Loaded {} garage{} from {}.", m_garages.size(), m_garages.size() == 1 ? "" : "s", config.garageFile));
	Log(std::format("{} name prefixes from {} (read again whenever it changes).", m_prefixes.size(), config.prefixesFile));
	Log(std::format("Log ins are checked against the account server with fingerprint {}.", Net::Fingerprint(m_accountIdentity)));
	// Players can check this against what their game remembers for this server.
	Log(std::format("Identity fingerprint: {}", Net::Fingerprint(m_identity->PublicKey())));
	return true;
}

void GarageServer::Stop()
{
	if (SaveAll())
		Log("Garages and profiles saved.");
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
	UpdateVotes(deltaTime);

	// The clock.
	if (m_config.dayLengthMinutes > 0.f)
		m_timeOfDay = std::fmod(m_timeOfDay + deltaTime * 24.f / (m_config.dayLengthMinutes * 60.f), 24.f);
	m_clockTimer += deltaTime;
	if (m_clockTimer >= CLOCK_INTERVAL)
	{
		m_clockTimer = 0.f;
		SendToPlaying(Writer().Word("clock").Num(m_timeOfDay).Num(m_config.dayLengthMinutes).Text());
	}
	// The prefixes can be changed with the server running.
	m_prefixesTimer += deltaTime;
	if (m_prefixesTimer >= PREFIXES_INTERVAL)
	{
		m_prefixesTimer = 0.f;
		if (LoadPrefixes())
			RenamePlayers();
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
		// Not in a garage yet: making one, or asking to join one (or taking that back).
		else if (type == "create" && (peer.state == PeerState::CHOOSING || peer.state == PeerState::REQUESTING))
		{
			if (peer.state == PeerState::REQUESTING)
				CancelRequest(peer);
			CreateGarage(peer);
		}
		else if (type == "request" && peer.state == PeerState::CHOOSING)
		{
			int garage = static_cast<int>(in.Int());
			if (!in.Failed())
				RequestGarage(peer, garage);
		}
		else if (type == "cancel" && peer.state == PeerState::REQUESTING)
		{
			CancelRequest(peer);
			SendChoices(peer);
		}
		else if (peer.state != PeerState::PLAYING)
			continue;
		else if (type == "ops")
		{
			NetProtocol::Batch batch;
			if (NetProtocol::ReadOps(message, batch))
				ApplyOps(peer.garage, peer.id, batch.sequence, std::move(batch.ops));
		}
		else if (type == "ballot")
		{
			int requester = static_cast<int>(in.Int());
			bool yes = in.Int() != 0;
			if (!in.Failed())
				Ballot(peer, requester, yes);
		}
		else if (type == "presence")
		{
			// Passed on to everyone else, in every garage (each game puts them on their garage's plot), as
			// long as it's who it says it is. Where they are is kept in their profile here, for when they
			// come back.
			if (in.Int() != peer.id)
				continue;
			SendToPlaying(message, peer.id);
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
		refusal = std::format("{} is already on this server.", ticket->username);
	else if (slot == 0)
		refusal = std::format("That server is full ({} players).", m_config.maxPlayers);
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
	peer.prefix = PrefixOf(peer.name);
	Log(std::format("{} (account {}) joined from {}.", peer.name, peer.id, peer.connection->GetAddress()));

	// Back to their own garage; or, the first time here, a garage to choose.
	if (const Garage* garage = FindGarage(peer.profile->garage); garage && garage->members.contains(peer.id))
	{
		Enter(peer, garage->id);
		return;
	}
	peer.profile->garage = 0;
	SendChoices(peer);
}

void GarageServer::Enter(Peer& peer, int garageId)
{
	Garage& garage = *FindGarage(garageId);
	peer.garage = garageId;
	// A brand new garage: the first player's fresh shop becomes it, and anyone else in it waits for that.
	if (garage.records.empty())
	{
		if (garage.seeder == 0)
		{
			garage.seeder = peer.id;
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
	const Garage& garage = *FindGarage(peer.garage);
	Writer welcome;
	welcome.Word("welcome").Int(peer.id).Int(peer.slot).Int(seed).Str(peer.prefix).Str(peer.name).Int(garage.plot).Str(garage.name)
		.Num(m_timeOfDay).Num(m_config.dayLengthMinutes);
	// Everyone playing, in every garage, on their garage's plot.
	std::vector<const Peer*> others;
	for (const auto& other : m_peers)
		if (other.state == PeerState::PLAYING && other.id != peer.id)
			others.push_back(&other);
	welcome.Int(others.size());
	for (const auto* other : others)
		welcome.Int(other->id).Str(other->prefix).Str(other->name).Int(m_garages.at(other->garage).plot);
	WritePlots(welcome);
	welcome.Int(garage.records.size());
	for (const auto& [key, text] : garage.records)
		welcome.Str(key).Str(text);
	// Their backpack as they left it, and where they stood.
	const auto& profile = *peer.profile;
	welcome.Str(profile.layout).Int(profile.placed).Num(profile.x).Num(profile.y).Num(profile.z).Num(profile.yaw).Num(profile.pitch);
	peer.connection->Send(welcome.Text());
	peer.state = PeerState::PLAYING;
	SendToPlaying(Writer().Word("joined").Int(peer.id).Str(peer.prefix).Str(peer.name).Int(garage.plot).Text(), peer.id);
	Log(std::format("{} is in {}.", peer.name, garage.name));
	// A member online: anyone waiting to join this garage gets their vote.
	StartVotes(garage.id);
}

void GarageServer::ApplyOps(int garageId, int author, long long sequence, std::vector<NetProtocol::Op> ops)
{
	using NetProtocol::OpKind;
	Garage* garage = FindGarage(garageId);
	if (!garage)
		return;
	for (auto& op : ops)
	{
		// Money and the tallies go up or down by what the player's did, from wherever they are now.
		if (op.kind == OpKind::ADD)
		{
			auto current = garage->records.find(op.key);
			double base = current != garage->records.end() ? NetProtocol::CounterValue(current->second) : 0.0;
			op = { OpKind::SET, op.key, NetProtocol::CounterText(base + NetProtocol::CounterValue(op.text)) };
		}
		if (op.kind == OpKind::ERASE)
			garage->records.erase(op.key);
		else
			garage->records[op.key] = op.text;
	}
	if (ops.empty())
		return;
	m_dirty = true;
	// To everyone in the garage, the author too: that's how they know the garage has it.
	SendToGarage(garageId, NetProtocol::OpsMessage(author, sequence, ops));
	if (author == garage->seeder && !garage->records.empty())
	{
		Log(std::format("{} has its first shop.", garage->name));
		garage->seeder = 0;
		WelcomeWaiting(garageId);
	}
}

void GarageServer::Departed(const Peer& peer)
{
	if (peer.state == PeerState::NEW)
		return;
	Log(std::format("{} (account {}) left.", peer.name, peer.id));
	// Gone while asking to join: the request goes with them.
	if (peer.state == PeerState::REQUESTING)
		for (size_t i = 0; i < m_requests.size(); i++)
			if (m_requests[i].requester == peer.id)
			{
				JoinRequest request = m_requests[i];
				m_requests.erase(m_requests.begin() + static_cast<std::ptrdiff_t>(i));
				if (request.voting)
					EndVote(request, std::format("{} left before the vote was over.", peer.name));
				break;
			}
	if (peer.state == PeerState::PLAYING)
	{
		SendToPlaying(Writer().Word("left").Int(peer.id).Text());

		// A delivery in their arms is put down. Their backpack stays theirs, out of everyone's reach,
		// till they're back.
		if (const Garage* garage = FindGarage(peer.garage))
		{
			std::vector<NetProtocol::Op> ops;
			for (const auto& [key, text] : garage->records)
				if (auto dropped = NetProtocol::DroppedPackage(key, text, peer.id))
					ops.push_back({ NetProtocol::OpKind::SET, key, *dropped });
			ApplyOps(peer.garage, NetProtocol::SERVER_AUTHOR, 0, std::move(ops));
		}
	}
	if (Garage* garage = FindGarage(peer.garage); garage && peer.id == garage->seeder)
	{
		garage->seeder = 0;
		WelcomeWaiting(garage->id);
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
		Log("Couldn't save the garages: " + error);
		saved = false;
	}
	if (m_profiles.IsDirty() && !m_profiles.Save(error))
	{
		Log("Couldn't save the profiles: " + error);
		saved = false;
	}
	return saved;
}

void GarageServer::WelcomeWaiting(int garageId)
{
	Garage* garage = FindGarage(garageId);
	if (!garage)
		return;
	for (auto& peer : m_peers)
	{
		if (peer.state != PeerState::WAITING || peer.garage != garageId)
			continue;
		if (garage->records.empty())
		{
			if (garage->seeder != 0)
				return;
			garage->seeder = peer.id;
			Welcome(peer, true);
		}
		else
			Welcome(peer, false);
	}
}

void GarageServer::SendChoices(Peer& peer)
{
	peer.state = PeerState::CHOOSING;
	peer.garage = 0;
	Writer out;
	out.Word("garages").Int(m_garages.size());
	for (const auto& [id, garage] : m_garages)
		out.Int(id).Str(garage.name).Int(garage.members.size()).Int(OnlineMembers(id));
	peer.connection->Send(out.Text());
}

void GarageServer::CreateGarage(Peer& peer)
{
	Garage garage;
	garage.id = m_nextGarage++;
	garage.name = std::format("{}'s garage", peer.name);
	garage.plot = FreePlot();
	garage.members.insert(peer.id);
	m_garages[garage.id] = garage;
	peer.profile->garage = garage.id;
	m_profiles.MarkDirty();
	m_dirty = true;
	Log(std::format("{} made {}, on plot {}.", peer.name, garage.name, garage.plot));
	// Everyone sees it go up on its plot.
	SendPlots();
	Enter(peer, garage.id);
}

void GarageServer::RequestGarage(Peer& peer, int garageId)
{
	Garage* garage = FindGarage(garageId);
	if (!garage)
	{
		SendChoices(peer);
		return;
	}
	// One nobody belongs to: theirs for the asking.
	if (garage->members.empty())
	{
		garage->members.insert(peer.id);
		peer.profile->garage = garageId;
		m_profiles.MarkDirty();
		m_dirty = true;
		Log(std::format("{} took over {}, which had no members.", peer.name, garage->name));
		Enter(peer, garageId);
		return;
	}
	m_requests.push_back({ peer.id, garageId });
	peer.state = PeerState::REQUESTING;
	peer.garage = garageId;
	Log(std::format("{} asked to join {}.", peer.name, garage->name));
	if (OnlineMembers(garageId) > 0)
		StartVotes(garageId);
	else
		peer.connection->Send(Writer().Word("pending")
			.Str(std::format("Nobody from {} is online. Your request waits for one of them: the vote starts when they come in.", garage->name)).Text());
}

void GarageServer::CancelRequest(Peer& peer)
{
	for (size_t i = 0; i < m_requests.size(); i++)
		if (m_requests[i].requester == peer.id)
		{
			JoinRequest request = m_requests[i];
			m_requests.erase(m_requests.begin() + static_cast<std::ptrdiff_t>(i));
			if (request.voting)
				EndVote(request, std::format("{} took back their request.", peer.name));
			break;
		}
	peer.state = PeerState::CHOOSING;
	peer.garage = 0;
}

void GarageServer::Ballot(Peer& peer, int requester, bool yes)
{
	const Garage* garage = FindGarage(peer.garage);
	if (!garage || !garage->members.contains(peer.id))
		return;
	for (auto& request : m_requests)
		if (request.requester == requester && request.garage == peer.garage && request.voting)
		{
			request.ballots[peer.id] = yes;
			break;
		}
}

void GarageServer::StartVotes(int garageId)
{
	const Garage* garage = FindGarage(garageId);
	if (!garage || OnlineMembers(garageId) == 0)
		return;
	for (auto& request : m_requests)
	{
		if (request.garage != garageId || request.voting)
			continue;
		Peer* requester = FindPeer(request.requester);
		if (!requester)
			continue;
		request.voting = true;
		request.timeLeft = NetProtocol::VOTE_SECONDS;
		request.ballots.clear();
		SendToGarage(garageId, Writer().Word("vote").Int(request.requester).Str(FullName(requester->prefix, requester->name))
			.Num(NetProtocol::VOTE_SECONDS).Text());
		requester->connection->Send(Writer().Word("pending")
			.Str(std::format("The members of {} online are voting on letting you in.", garage->name)).Text());
	}
}

void GarageServer::UpdateVotes(float deltaTime)
{
	for (size_t i = 0; i < m_requests.size();)
	{
		JoinRequest& request = m_requests[i];
		if (!request.voting)
		{
			i++;
			continue;
		}
		const Garage* garage = FindGarage(request.garage);
		int online = OnlineMembers(request.garage);
		// Its members have all gone: it waits again for one of them.
		if (online == 0)
		{
			request.voting = false;
			if (Peer* requester = FindPeer(request.requester))
				requester->connection->Send(Writer().Word("pending")
					.Str(std::format("The members of {} went offline before deciding. Your request waits for one of them to come back.", garage->name)).Text());
			i++;
			continue;
		}
		// Only the votes of the members still here count.
		int yes = 0, no = 0;
		for (const auto& [member, ballot] : request.ballots)
		{
			const Peer* voter = FindPeer(member);
			if (!voter || voter->state != PeerState::PLAYING || voter->garage != request.garage)
				continue;
			(ballot ? yes : no)++;
		}
		request.timeLeft -= deltaTime;
		std::optional<bool> decided;
		if (yes * 2 > online)
			decided = true;
		else if (no * 2 >= online)
			decided = false;
		else if (request.timeLeft <= 0.f)
			decided = yes > no;
		if (!decided)
		{
			i++;
			continue;
		}
		JoinRequest done = request;
		m_requests.erase(m_requests.begin() + static_cast<std::ptrdiff_t>(i));
		if (*decided)
			Admit(done);
		else
			TurnAway(done, yes + no == 0 ? std::format("Nobody in {} answered in time.", garage->name)
				: std::format("{} voted not to let you in.", garage->name));
	}
}

void GarageServer::Admit(JoinRequest request)
{
	Garage& garage = *FindGarage(request.garage);
	Peer* requester = FindPeer(request.requester);
	if (!requester)
		return;
	garage.members.insert(requester->id);
	requester->profile->garage = garage.id;
	m_profiles.MarkDirty();
	m_dirty = true;
	Log(std::format("{} voted {} in.", garage.name, requester->name));
	EndVote(request, std::format("{} was let in.", FullName(requester->prefix, requester->name)));
	Enter(*requester, garage.id);
}

void GarageServer::TurnAway(JoinRequest request, const std::string& why)
{
	Peer* requester = FindPeer(request.requester);
	if (!requester)
		return;
	Log(std::format("{} was turned away from {}.", requester->name, FindGarage(request.garage)->name));
	EndVote(request, std::format("{} was turned away.", FullName(requester->prefix, requester->name)));
	requester->connection->Send(Writer().Word("denied").Str(why).Text());
	SendChoices(*requester);
}

void GarageServer::EndVote(const JoinRequest& request, const std::string& outcome)
{
	SendToGarage(request.garage, Writer().Word("voteover").Int(request.requester).Str(outcome).Text());
}

void GarageServer::SendPlots()
{
	Writer out;
	out.Word("plots");
	WritePlots(out);
	SendToPlaying(out.Text());
}

Writer& GarageServer::WritePlots(Writer& out) const
{
	out.Int(m_garages.size());
	for (const auto& [id, garage] : m_garages)
		out.Int(garage.plot);
	return out;
}

void GarageServer::SendToPlaying(const std::string& message, int except)
{
	for (auto& peer : m_peers)
		if (peer.state == PeerState::PLAYING && peer.id != except)
			peer.connection->Send(message);
}

void GarageServer::SendToGarage(int garage, const std::string& message, int except)
{
	for (auto& peer : m_peers)
		if (peer.state == PeerState::PLAYING && peer.garage == garage && peer.id != except)
			peer.connection->Send(message);
}

GarageServer::Peer* GarageServer::FindPeer(int account)
{
	for (auto& peer : m_peers)
		if (peer.state != PeerState::NEW && peer.id == account)
			return &peer;
	return nullptr;
}

GarageServer::Garage* GarageServer::FindGarage(int id)
{
	auto it = m_garages.find(id);
	return it == m_garages.end() ? nullptr : &it->second;
}

int GarageServer::OnlineMembers(int garage) const
{
	return static_cast<int>(std::count_if(m_peers.begin(), m_peers.end(),
		[&](const Peer& peer) { return peer.state == PeerState::PLAYING && peer.garage == garage; }));
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

int GarageServer::FreePlot() const
{
	for (int plot = 0;; plot++)
		if (std::none_of(m_garages.begin(), m_garages.end(), [&](const auto& entry) { return entry.second.plot == plot; }))
			return plot;
}

bool GarageServer::IsLoggedIn(int account) const
{
	return std::any_of(m_peers.begin(), m_peers.end(), [&](const Peer& peer) { return peer.state != PeerState::NEW && peer.id == account; });
}

bool GarageServer::Load(std::string& error)
{
	m_garages.clear();
	m_nextGarage = 1;
	if (!std::filesystem::exists(m_config.garageFile))
		return true;
	std::ifstream file(m_config.garageFile, std::ios::binary);
	std::stringstream text;
	text << file.rdbuf();
	Reader in(text.str());
	in.Expect("lvgarage");
	long long version = in.Int();
	if (version < 1 || version > GARAGE_FILE_VERSION)
		in.Fail("it was saved by a different version of the server");
	in.Expect("time");
	m_timeOfDay = static_cast<float>(in.Num());
	auto readRecords = [&](NetProtocol::Records& records) {
		in.Expect("records");
		long long count = in.Int();
		for (long long i = 0; i < count && !in.Failed(); i++)
		{
			std::string key = in.Next();
			records[key] = in.Next();
		}
	};
	if (version < 2)
	{
		// One garage a server, before: it becomes the first, on the first plot, and nobody's a member of it
		// till they choose it again.
		Garage garage;
		garage.id = m_nextGarage++;
		garage.name = "The first garage";
		readRecords(garage.records);
		if (!garage.records.empty())
			m_garages[garage.id] = std::move(garage);
	}
	else
	{
		in.Expect("garages");
		m_nextGarage = static_cast<int>(in.Int());
		long long count = in.Int();
		for (long long i = 0; i < count && !in.Failed(); i++)
		{
			Garage garage;
			in.Expect("garage");
			garage.id = static_cast<int>(in.Int());
			garage.name = in.Next();
			garage.plot = static_cast<int>(in.Int());
			in.Expect("members");
			long long members = in.Int();
			for (long long m = 0; m < members && !in.Failed(); m++)
				garage.members.insert(static_cast<int>(in.Int()));
			readRecords(garage.records);
			m_nextGarage = (std::max)(m_nextGarage, garage.id + 1);
			m_garages[garage.id] = std::move(garage);
		}
	}
	if (in.Failed())
	{
		error = std::format("{} couldn't be read: {}.", m_config.garageFile, in.Error());
		return false;
	}
	m_timeOfDay = std::fmod(std::abs(m_timeOfDay), 24.f);

	// Nobody's here yet: a delivery anyone was carrying when the server last stopped is put down.
	for (auto& [id, garage] : m_garages)
		for (auto& [key, text] : garage.records)
			if (auto dropped = NetProtocol::DroppedPackage(key, text, std::nullopt))
				text = *dropped;
	return true;
}

bool GarageServer::Save(std::string& error)
{
	if (m_garages.empty())
		return true;
	Writer out;
	out.Word("lvgarage").Int(GARAGE_FILE_VERSION).Line();
	out.Word("time").Num(m_timeOfDay).Line();
	out.Word("garages").Int(m_nextGarage).Int(m_garages.size()).Line();
	for (const auto& [id, garage] : m_garages)
	{
		out.Word("garage").Int(garage.id).Str(garage.name).Int(garage.plot).Word("members").Int(garage.members.size());
		for (int member : garage.members)
			out.Int(member);
		out.Line();
		out.Word("records").Int(garage.records.size()).Line();
		for (const auto& [key, text] : garage.records)
			out.Str(key).Str(text).Line();
	}

	// Written beside the old one, then swapped in, so a crash mid-save never loses the garages.
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

bool GarageServer::LoadPrefixes()
{
	if (!std::filesystem::exists(m_config.prefixesFile))
	{
		std::ofstream file(m_config.prefixesFile);
		file << "# Prefixes shown in front of players' names: over their heads, in the player list and when\n";
		file << "# they come and go. One player a line, username=prefix. The server reads this again whenever\n";
		file << "# it changes: no need to restart it.\n";
		file << "#joey=[Owner]\n";
		file << "#sam=[VIP]\n";
	}
	std::ifstream file(m_config.prefixesFile, std::ios::binary);
	if (!file.is_open())
		return false;
	std::stringstream contents;
	contents << file.rdbuf();
	if (contents.str() == m_prefixesText)
		return false;
	m_prefixesText = contents.str();

	auto trim = [](std::string text) {
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
			text.pop_back();
		size_t start = 0;
		while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
			start++;
		return text.substr(start);
	};
	auto lower = [](std::string text) {
		for (char& c : text)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return text;
	};
	std::map<std::string, std::string> prefixes;
	std::string line;
	while (std::getline(contents, line))
	{
		line = trim(line);
		if (line.empty() || line[0] == '#')
			continue;
		auto equals = line.find('=');
		if (equals == std::string::npos)
			continue;
		// Printable text only, and not too long to sit over a head.
		std::string prefix;
		for (char c : trim(line.substr(equals + 1)))
			if (std::isprint(static_cast<unsigned char>(c)) && prefix.size() < PREFIX_MAX)
				prefix += c;
		std::string username = lower(trim(line.substr(0, equals)));
		if (!username.empty() && !prefix.empty())
			prefixes[username] = trim(prefix);
	}
	bool changed = prefixes != m_prefixes;
	m_prefixes = std::move(prefixes);
	return changed;
}

void GarageServer::RenamePlayers()
{
	Log(std::format("Name prefixes changed: {} now.", m_prefixes.size()));
	for (auto& peer : m_peers)
	{
		if (peer.state == PeerState::NEW)
			continue;
		std::string prefix = PrefixOf(peer.name);
		if (prefix == peer.prefix)
			continue;
		peer.prefix = prefix;
		// Everyone, them too: their own name in the player list.
		if (peer.state == PeerState::PLAYING)
			SendToPlaying(Writer().Word("name").Int(peer.id).Str(peer.prefix).Str(peer.name).Text());
	}
}

std::string GarageServer::PrefixOf(const std::string& username) const
{
	std::string key = username;
	for (char& c : key)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	auto it = m_prefixes.find(key);
	return it == m_prefixes.end() ? "" : it->second;
}
