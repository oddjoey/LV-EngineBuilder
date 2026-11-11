// The desert quarry: boulders broken by hand for what the stone fetches, and the rock crusher on the plot that earns on
// its own, both upgraded with the money they make.
#include "Workshop.h"

#include <algorithm>
#include <format>

#include "../Save/SaveText.h"

namespace
{
	struct Track
	{
		const char* blurb;
		std::vector<const char*> names;		// by level, the first for none (or the starting one)
		std::vector<double> prices;			// to reach each level after the first
	};

	const Track& TrackOf(QuarryUpgrade upgrade)
	{
		static const Track tracks[QUARRY_UPGRADES] = {
			{ "Crushes rock dug from the plot into gravel and sells it by the yard: money coming in while you work on engines.",
				{ "No crusher", "Second-hand jaw crusher", "Jaw crusher", "Cone crusher", "Twin cone crushers", "Crushing plant" },
				{ 350.0, 900.0, 2200.0, 5500.0, 13000.0 } },
			{ "Feeds the crusher steadily instead of a shovel at a time: more out of whatever crusher there is.",
				{ "Fed by shovel", "Short conveyor", "Long conveyor", "Grizzly feeder and conveyor" },
				{ 800.0, 3000.0, 9000.0 } },
			{ "How much gravel piles up before the crusher has to stop: empty it before it's full, or have someone do it.",
				{ "Heap on the ground", "Wooden bin", "Steel hopper", "Twin hoppers", "Loading bunker" },
				{ 150.0, 600.0, 2000.0, 6000.0 } },
			{ "What the desert's boulders are broken with by hand: quicker, and less stone wasted.",
				{ "Pickaxe", "Forged steel pickaxe", "Pick, rock bar and wedges", "Pneumatic breaker" },
				{ 120.0, 700.0, 2500.0 } },
			{ "Someone to empty the hopper and haul the gravel away whenever it fills, for a tenth of what it fetches.",
				{ "Nobody", "A hired hand" },
				{ 2500.0 } },
		};
		return tracks[static_cast<int>(upgrade)];
	}

	// The crusher's dollars a minute at each level, and the conveyor's push on it.
	constexpr double CRUSHER_RATE[] = { 0.0, 4.0, 9.0, 20.0, 45.0, 100.0 };
	constexpr double CONVEYOR_FACTOR[] = { 1.0, 1.5, 2.2, 3.2 };
	constexpr double HOPPER_HOLDS[] = { 60.0, 200.0, 600.0, 1800.0, 5000.0 };
	constexpr float HAMMER_SECONDS[] = { 3.f, 2.2f, 1.5f, 0.9f };
	constexpr double HAMMER_PAY[] = { 1.0, 1.6, 2.5, 4.0 };
	constexpr double HAND_CUT = 0.1;
}

const QuarryState& Workshop::GetQuarry() const
{
	return m_quarry;
}

const char* Workshop::QuarryUpgradeName(QuarryUpgrade upgrade, int level)
{
	const auto& names = TrackOf(upgrade).names;
	return names[std::clamp(level, 0, static_cast<int>(names.size()) - 1)];
}

const char* Workshop::QuarryUpgradeBlurb(QuarryUpgrade upgrade)
{
	return TrackOf(upgrade).blurb;
}

int Workshop::QuarryMaxLevel(QuarryUpgrade upgrade)
{
	return static_cast<int>(TrackOf(upgrade).prices.size());
}

double Workshop::QuarryUpgradePrice(QuarryUpgrade upgrade) const
{
	int level = m_quarry.levels[static_cast<int>(upgrade)];
	const auto& prices = TrackOf(upgrade).prices;
	return level < static_cast<int>(prices.size()) ? prices[level] : 0.0;
}

std::optional<std::string> Workshop::QuarryUpgradeProblem(QuarryUpgrade upgrade) const
{
	if (QuarryUpgradePrice(upgrade) <= 0.0)
		return "It's as good as it gets.";
	bool crusher = m_quarry.levels[static_cast<int>(QuarryUpgrade::CRUSHER)] > 0;
	if (!crusher && (upgrade == QuarryUpgrade::CONVEYOR || upgrade == QuarryUpgrade::HOPPER || upgrade == QuarryUpgrade::HAND))
		return "Set up a crusher first.";
	if (QuarryUpgradePrice(upgrade) > m_money)
		return std::format("${:.0f} short.", QuarryUpgradePrice(upgrade) - m_money);
	return std::nullopt;
}

bool Workshop::BuyQuarryUpgrade(QuarryUpgrade upgrade)
{
	if (auto problem = QuarryUpgradeProblem(upgrade))
	{
		Log(*problem);
		return false;
	}
	int& level = m_quarry.levels[static_cast<int>(upgrade)];
	if (!Spend(QuarryUpgradePrice(upgrade), QuarryUpgradeName(upgrade, level + 1)))
		return false;
	level++;
	Log(std::format("The quarry has a {} now.", QuarryUpgradeName(upgrade, level)));
	return true;
}

double Workshop::QuarryRate() const
{
	int crusher = m_quarry.levels[static_cast<int>(QuarryUpgrade::CRUSHER)];
	int conveyor = m_quarry.levels[static_cast<int>(QuarryUpgrade::CONVEYOR)];
	return CRUSHER_RATE[crusher] * CONVEYOR_FACTOR[conveyor];
}

double Workshop::QuarryCapacity() const
{
	return HOPPER_HOLDS[m_quarry.levels[static_cast<int>(QuarryUpgrade::HOPPER)]];
}

float Workshop::BoulderSeconds() const
{
	return HAMMER_SECONDS[m_quarry.levels[static_cast<int>(QuarryUpgrade::HAMMER)]];
}

double Workshop::BoulderPay(double size, double worth) const
{
	return (6.0 + 10.0 * std::clamp(size, 0.5, 1.5)) * worth * HAMMER_PAY[m_quarry.levels[static_cast<int>(QuarryUpgrade::HAMMER)]];
}

double Workshop::BreakBoulder(double size, double worth)
{
	double pay = BoulderPay(size, worth);
	m_money += pay;
	m_stats.moneyEarned += pay;
	m_quarry.earned += pay;
	m_quarry.bouldersBroken++;
	return pay;
}

void Workshop::AdvanceQuarry(double seconds)
{
	double rate = QuarryRate();
	if (rate <= 0.0 || seconds <= 0.0)
		return;
	m_quarry.hopper = (std::min)(QuarryCapacity(), m_quarry.hopper + rate * seconds / 60.0);
	// The hand hauls it away as it comes, keeping a tenth.
	if (m_quarry.levels[static_cast<int>(QuarryUpgrade::HAND)] > 0 && m_quarry.hopper > 0.0)
	{
		double pay = m_quarry.hopper * (1.0 - HAND_CUT);
		m_money += pay;
		m_stats.moneyEarned += pay;
		m_quarry.earned += pay;
		m_quarry.hopper = 0.0;
	}
}

double Workshop::CollectQuarry()
{
	double pay = m_quarry.hopper;
	if (pay <= 0.0)
		return 0.0;
	m_money += pay;
	m_stats.moneyEarned += pay;
	m_quarry.earned += pay;
	m_quarry.hopper = 0.0;
	Log(std::format("Sold ${:.0f} of gravel from the crusher's hopper.", pay));
	return pay;
}

std::string Workshop::QuarryText() const
{
	Writer out;
	for (int level : m_quarry.levels)
		out.Int(level);
	out.Num(m_quarry.hopper).Num(m_quarry.earned).Int(m_quarry.bouldersBroken);
	return out.Text();
}

bool Workshop::RestoreQuarry(const std::string& text)
{
	Reader in(text);
	QuarryState quarry;
	for (int i = 0; i < QUARRY_UPGRADES; i++)
		quarry.levels[i] = std::clamp(static_cast<int>(in.Int()), 0, QuarryMaxLevel(static_cast<QuarryUpgrade>(i)));
	quarry.hopper = (std::max)(0.0, in.Num());
	quarry.earned = (std::max)(0.0, in.Num());
	quarry.bouldersBroken = (std::max)(0, static_cast<int>(in.Int()));
	if (in.Failed())
		return false;
	m_quarry = quarry;
	return true;
}
