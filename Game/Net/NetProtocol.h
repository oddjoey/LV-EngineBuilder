#pragma once

#include <cctype>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../Save/SaveText.h"

// What the game and the garage server say to each other, shared by both. The server doesn't know
// how a shop works: it keeps the garage as keyed records of save text, puts every change from the
// players in one order, and passes each change on to everyone.
//
// Players log in once, to the account server (LV-AccountServer), which keeps every account. It hands
// back a ticket it has signed: who they are, until when. Any garage server takes the ticket as proof,
// checking the signature against the account server's identity, which it's given as a file. An
// account's number is the player's id for good, so what's in their backpack stays theirs (carried by
// them, in the garage's records) while they're away, and comes back with them.
//
// Messages, each one block of save text. Both servers start the same way:
//   game -> server   secure <version> <key>                 starts the encrypted connection (LV/Engine/Networking/Crypto.h)
//   server -> game   secure <key> <identity> <signature>    and everything after this, both ways, is encrypted
//
// To the account server:
//   game -> server   login <new account> "username" "password"
//   server -> game   ticket "ticket" "signature"            see Ticket
//                    reject "why"
//
// To a garage server:
//   game -> server   query                                  what the server is, for the server list (in the clear)
//                    hello <version> "ticket" "signature"
//                    ops <author> <sequence> <count> (<kind> "key" "text")...
//                    presence <id> "name" <x> <y> <z> <yaw> <pitch> <arms> <crouching> <fur> <skin> <cap> <tail>
//                                                           arms: 0 down, 1 the right one out (holding something), 2 both (carrying a delivery)
//                    profile "layout"                       how their backpack and hotbar are laid out
// 
//   server -> game   info <version> "name" "region" <players> <max>
//                    welcome <id> <slot> <seed> <time> <day length> <count> (<id> "name")...
//                            <count> ("key" "text")... "layout" <placed> <x> <y> <z> <yaw> <pitch>
//                    reject "why"
//                    ops ...     (the same, with the author's number and sequence; author -1 is the server)
//                    presence ...
//                    clock <time> <day length>
//                    left <id>

namespace NetProtocol
{
	// Both ends have to be the same game: the records are its save text.
	constexpr long long VERSION = 12;
	constexpr uint16_t DEFAULT_PORT = 33333;
	constexpr uint16_t ACCOUNT_PORT = 44444;
	constexpr const char* ACCOUNT_IP = "localhost";
	// Up to 63 players at once. Each one online has a slot, 1 to 63, for the numbers they give new
	// parts and deliveries: a band of their own, so two players buying at once never give two parts
	// the same number. (Band 0 is the garage's own: whatever it started with.)
	constexpr int MAX_SLOTS = 63;
	constexpr uint32_t ID_BAND = 1u << 26;
	constexpr int SERVER_AUTHOR = -1;
	constexpr size_t USERNAME_MIN = 3;
	constexpr size_t USERNAME_MAX = 20;
	constexpr size_t PASSWORD_MIN = 6;
	constexpr size_t PASSWORD_MAX = 64;

	// Letters, digits, - and _: why not, or empty if it's fine.
	inline std::string UsernameProblem(const std::string& username)
	{
		if (username.size() < USERNAME_MIN || username.size() > USERNAME_MAX)
			return "A username is 3 to 20 letters long.";
		for (char c : username)
			if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
				return "A username can only have letters, digits, - and _.";
		return "";
	}

	// Proof of who a player is, from the account server: the text is signed with its identity key, and
	// is good until it expires (seconds since 1970).
	struct Ticket
	{
		int account = 0;
		std::string username = "";
		long long issued = 0;
		long long expires = 0;
	};
	// Some slack for the servers' clocks not quite agreeing.
	constexpr long long TICKET_CLOCK_SLACK = 300;

	inline long long UnixNow()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}
	inline std::string TicketText(const Ticket& ticket)
	{
		return Writer().Word("lvticket").Int(1).Int(ticket.account).Str(ticket.username).Int(ticket.issued).Int(ticket.expires).Text();
	}
	inline std::optional<Ticket> ReadTicket(const std::string& text)
	{
		Reader in(text);
		in.Expect("lvticket");
		if (in.Int() != 1)
			return std::nullopt;
		Ticket ticket;
		ticket.account = static_cast<int>(in.Int());
		ticket.username = in.Next();
		ticket.issued = in.Int();
		ticket.expires = in.Int();
		if (in.Failed() || ticket.account < 1)
			return std::nullopt;
		return ticket;
	}
	inline bool TicketExpired(const Ticket& ticket)
	{
		long long now = UnixNow();
		return now > ticket.expires + TICKET_CLOCK_SLACK || now < ticket.issued - TICKET_CLOCK_SLACK;
	}

	// The garage: one line of save text per part, item, delivery and so on, by key.
	using Records = std::map<std::string, std::string>;

	enum class OpKind { SET = 0, ERASE = 1, ADD = 2 };
	struct Op
	{
		OpKind kind = OpKind::SET;
		std::string key = "";
		std::string text = "";
	};
	struct Batch
	{
		int author = 0;
		long long sequence = 0;
		std::vector<Op> ops = {};
	};

	inline std::string OpsMessage(int author, long long sequence, const std::vector<Op>& ops)
	{
		Writer out;
		out.Word("ops").Int(author).Int(sequence).Int(ops.size());
		for (const auto& op : ops)
			out.Int(static_cast<int>(op.kind)).Str(op.key).Str(op.text);
		return out.Text();
	}

	inline bool ReadOps(const std::string& message, Batch& batch)
	{
		Reader in(message);
		in.Expect("ops");
		batch.author = static_cast<int>(in.Int());
		batch.sequence = in.Int();
		size_t count = in.Count();
		for (size_t i = 0; i < count && !in.Failed(); i++)
		{
			Op op;
			op.kind = in.Enumerated<OpKind>(3);
			op.key = in.Next();
			op.text = in.Next();
			batch.ops.push_back(std::move(op));
		}
		return !in.Failed();
	}

	// Records whose changes add up rather than replace (money, hours, the career tallies): two players
	// spending at once both come off.
	inline bool IsCounter(const std::string& key)
	{
		return key.compare(0, 2, "c:") == 0;
	}
	inline double CounterValue(const std::string& text)
	{
		Reader in(text);
		double value = in.Num();
		return in.Failed() ? 0.0 : value;
	}
	inline std::string CounterText(double value)
	{
		return Writer().Num(value).Text();
	}

	namespace Detail
	{
		// The first count tokens of a record, split at the spaces, and the rest of it untouched.
		// Only for records that start with plain numbers.
		inline std::optional<std::vector<std::string>> Leading(const std::string& text, size_t count, std::string& rest)
		{
			std::vector<std::string> tokens;
			size_t at = 0;
			while (tokens.size() < count)
			{
				at = text.find_first_not_of(' ', at);
				if (at == std::string::npos || text[at] == '"')
					return std::nullopt;
				size_t end = text.find(' ', at);
				if (end == std::string::npos)
					end = text.size();
				tokens.push_back(text.substr(at, end - at));
				at = end;
			}
			rest = text.substr(at);
			return tokens;
		}
	}

	// A player has gone: a delivery in their arms drops where they stood (player: theirs; none:
	// anyone's). What's in their backpack and hand stays theirs, for when they're back. Nullopt for
	// anything else.
	inline std::optional<std::string> DroppedPackage(const std::string& key, const std::string& text, std::optional<int> player)
	{
		if (key.compare(0, 2, "k:") != 0)
			return std::nullopt;
		// <crate> <carrier> <position x3> <rotation x4> <velocity x3> <resting> ...
		std::string rest;
		auto tokens = Detail::Leading(text, 13, rest);
		if (!tokens || (*tokens)[1] == "-1" || (player && (*tokens)[1] != std::to_string(*player)))
			return std::nullopt;
		(*tokens)[1] = "-1";
		(*tokens)[12] = "0";
		Writer out;
		for (const auto& token : *tokens)
			out.Word(token);
		return out.Text() + rest;
	}
}
