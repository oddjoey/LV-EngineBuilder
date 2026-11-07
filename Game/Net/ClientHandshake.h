#pragma once

#include <string>

#include "LV/Engine/Networking/Crypto.h"
#include "LV/Engine/Networking/Socket.h"
#include "NetProtocol.h"

// The game's half of the handshake that starts every connection to a server, the account server or
// a garage (see NetProtocol): our key goes first, then the server's answer is checked and the
// connection encrypted, before anything private is sent.
namespace ClientHandshake
{
	// Each server's identity, from the first time the game reached it.
	constexpr const char* KNOWN_SERVERS_FILE = "known_servers.txt";

	inline void Start(Net::Connection& connection, const Net::KeyExchange& keyExchange)
	{
		connection.Send(Writer().Word("secure").Int(NetProtocol::VERSION).Word(keyExchange.PublicKey()).Text());
	}

	// The server's "secure" answer (after the word itself). Empty once the connection is encrypted;
	// otherwise why not, and nothing should be sent.
	inline std::string Finish(Reader& in, Net::Connection& connection, const Net::KeyExchange& keyExchange, const std::string& host)
	{
		std::string serverKey = in.Next();
		std::string identity = in.Next();
		std::string signature = in.Next();
		const std::string& clientKey = keyExchange.PublicKey();

		// The server proves it holds its identity key by signing both sides' keys: someone in between
		// swapping keys can't.
		if (in.Failed() || !Net::VerifySignature(identity, Net::HandshakeTranscript(clientKey, serverKey), signature))
			return std::format("{} couldn't prove who it is, so nothing was sent to it.", host);
		// And it's the server that was at this address before.
		if (Net::KnownHosts(KNOWN_SERVERS_FILE).Check(host, Net::Fingerprint(identity)) == Net::KnownHosts::Result::CHANGED)
			return std::format("{} isn't the server that was there before: its identity has changed, so nothing was sent to it. "
				"If its owner changed it on purpose, delete its line from {} and try again.", host, KNOWN_SERVERS_FILE);

		std::unique_ptr<Net::Cipher> sending, receiving;
		if (!keyExchange.Derive(serverKey, false, clientKey, serverKey, sending, receiving))
			return std::format("Couldn't make a safe connection to {}.", host);
		connection.Encrypt(std::move(sending), std::move(receiving));
		return "";
	}
}
