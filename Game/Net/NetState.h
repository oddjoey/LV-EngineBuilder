#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "NetProtocol.h"

class Workshop;
class ShopWorld;
class Part;
struct ItemVisual;
struct EngineSpec;

// The shop the players share, as records: one per part, item, delivery, engine and piece of
// equipment, plus the money, tallies and switches. Each is a line of save-file text under a key, so
// a change to one part sends just that part.
using StateRecords = NetProtocol::Records;

// A record set to new text, or removed (no text).
struct StateChange
{
	std::string key = "";
	std::optional<std::string> text = std::nullopt;
};

// Turns the shop into records and back. What each player carries is theirs: it's written as carried
// by them, and someone else's carried things are hidden here (or shown in their hand).
class NetState
{
public:
	// Everything shared, as it is here now. localPlayer marks what this player is carrying.
	static StateRecords Capture(const Workshop& workshop, const ShopWorld& world, int localPlayer);
	// Makes the shop here match the changes given, changing parts and items in place so anything
	// holding on to them keeps working. full: the changes are the whole shop, so anything here that
	// isn't among them goes.
	static void Apply(Workshop& workshop, ShopWorld& world, const std::vector<StateChange>& changes, int localPlayer, bool full = false);
	// Each player numbers the parts and deliveries they make from a band of their own (see
	// NetProtocol::ID_BAND), so two players buying at once never give two parts the same number.
	static void ClaimIds(const Workshop& workshop, ShopWorld& world, int slot);

	// How this player's backpack and hotbar are laid out, kept with their account. (What's in them is
	// in the records, carried by them.)
	static std::string CaptureLayout(const ShopWorld& world);
	// Lays the backpack and hotbar out as they were. Anything carried that the layout doesn't place
	// goes wherever it fits.
	static void RestoreLayout(ShopWorld& world, const std::string& layout);
private:
	// Into this player's backpack (or onto the hotbar), or onto the shelf if there's no room.
	static void Stow(ShopWorld& world, ItemVisual& visual);
	struct Counter;
	static const std::vector<Counter>& Counters();
	static std::string PartText(const Part& part);
	static std::shared_ptr<Part> ReadPart(const std::string& text, const EngineSpec& spec);
};
