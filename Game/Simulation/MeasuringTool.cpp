#include "MeasuringTool.h"

#include <algorithm>
#include <cmath>

#include "Random.h"

bool MeasuringTool::CanMeasure(const Feature& feature) const
{
	if (std::find(kinds.begin(), kinds.end(), feature.kind) == kinds.end())
		return false;

	double size = feature.spec.nominal;
	if (minSize > 0.0 && size < minSize)
		return false;
	if (maxSize > 0.0 && size > maxSize)
		return false;
	return true;
}

bool MeasuringTool::CanCheck(AssemblyCheck check) const
{
	return std::find(assemblyChecks.begin(), assemblyChecks.end(), check) != assemblyChecks.end();
}

double MeasuringTool::Read(double trueValue) const
{
	double reading = Random::Normal(trueValue, sigma);
	if (resolution > 0.0)
		reading = std::round(reading / resolution) * resolution;
	return reading;
}

namespace ToolCatalog
{
	const std::vector<MeasuringTool>& All()
	{
		static const std::vector<MeasuringTool> tools = [] {
			std::vector<MeasuringTool> list;

			MeasuringTool caliper;
			caliper.name = "Digital Caliper";
			caliper.description = "Quick and cheap. Fine for lengths, useless for clearances.";
			caliper.kinds = { FeatureKind::JOURNAL, FeatureKind::LENGTH, FeatureKind::THICKNESS };
			caliper.resolution = 0.01;
			caliper.sigma = 0.010;
			caliper.price = 40.0;
			caliper.hoursPerUse = 0.05;
			list.push_back(caliper);

			MeasuringTool micrometer;
			micrometer.name = "Outside Micrometer Set";
			micrometer.description = "Reads to a micron. Journals, skirts, shells and lengths.";
			micrometer.kinds = { FeatureKind::JOURNAL, FeatureKind::LENGTH, FeatureKind::THICKNESS };
			micrometer.resolution = 0.001;
			micrometer.sigma = 0.0012;
			micrometer.price = 350.0;
			micrometer.hoursPerUse = 0.15;
			list.push_back(micrometer);

			MeasuringTool telescoping;
			telescoping.name = "Telescoping Gauge";
			telescoping.description = "Feel-dependent bore measurement. Good enough to spot wear.";
			telescoping.kinds = { FeatureKind::BORE };
			telescoping.minSize = 12.0;
			telescoping.resolution = 0.01;
			telescoping.sigma = 0.006;
			telescoping.price = 60.0;
			telescoping.hoursPerUse = 0.2;
			list.push_back(telescoping);

			MeasuringTool boreGauge;
			boreGauge.name = "Dial Bore Gauge";
			boreGauge.description = "Zeroed off a setting ring. The right way to measure a bore.";
			boreGauge.kinds = { FeatureKind::BORE };
			boreGauge.minSize = 18.0;
			boreGauge.resolution = 0.001;
			boreGauge.sigma = 0.0015;
			boreGauge.price = 450.0;
			boreGauge.hoursPerUse = 0.25;
			list.push_back(boreGauge);

			MeasuringTool smallHole;
			smallHole.name = "Small Hole Gauge Set";
			smallHole.description = "Split-ball gauges for valve guides. Feel it, lock it, mic it.";
			smallHole.kinds = { FeatureKind::BORE };
			smallHole.maxSize = 13.0;
			smallHole.resolution = 0.005;
			smallHole.sigma = 0.004;
			smallHole.price = 45.0;
			smallHole.hoursPerUse = 0.15;
			list.push_back(smallHole);

			MeasuringTool smallBore;
			smallBore.name = "Dial Small-Bore Gauge";
			smallBore.description = "Indicating gauge for guides: reads bellmouth top and bottom in one go.";
			smallBore.kinds = { FeatureKind::BORE };
			smallBore.maxSize = 18.0;
			smallBore.resolution = 0.001;
			smallBore.sigma = 0.0012;
			smallBore.price = 380.0;
			smallBore.hoursPerUse = 0.15;
			list.push_back(smallBore);

			MeasuringTool runout;
			runout.name = "Runout Gauge & V-Blocks";
			runout.description = "Dial test indicator for valve face and seat runout.";
			runout.kinds = { FeatureKind::RUNOUT };
			runout.resolution = 0.002;
			runout.sigma = 0.002;
			runout.price = 140.0;
			runout.hoursPerUse = 0.1;
			list.push_back(runout);

			MeasuringTool springTester;
			springTester.name = "Valve Spring Tester";
			springTester.description = "Squeezes a spring to installed height and reads the force.";
			springTester.kinds = { FeatureKind::FORCE };
			springTester.resolution = 1.0;
			springTester.sigma = 1.5;
			springTester.price = 250.0;
			springTester.hoursPerUse = 0.05;
			list.push_back(springTester);

			MeasuringTool straightedge;
			straightedge.name = "Straightedge & Feelers";
			straightedge.description = "Lay it across the deck in a star pattern and find the gap.";
			straightedge.kinds = { FeatureKind::FLATNESS };
			straightedge.resolution = 0.01;
			straightedge.sigma = 0.005;
			straightedge.price = 80.0;
			straightedge.hoursPerUse = 0.1;
			list.push_back(straightedge);

			MeasuringTool burette;
			burette.name = "Burette & CC Plate";
			burette.description = "CC the chambers and dishes for a real compression ratio.";
			burette.kinds = { FeatureKind::VOLUME };
			burette.resolution = 0.1;
			burette.sigma = 0.05;
			burette.price = 90.0;
			burette.hoursPerUse = 0.2;
			list.push_back(burette);

			MeasuringTool profilometer;
			profilometer.name = "Surface Profilometer";
			profilometer.description = "Drags a stylus across the surface to read its finish (Ra).";
			profilometer.measuresFinish = true;
			profilometer.resolution = 0.01;
			profilometer.sigma = 0.02;
			profilometer.price = 1500.0;
			profilometer.hoursPerUse = 0.1;
			list.push_back(profilometer);

			MeasuringTool plastigauge;
			plastigauge.name = "Plastigauge";
			plastigauge.description = "Crushable thread under a torqued bearing cap. Compare the width to the card.";
			plastigauge.assemblyChecks = { AssemblyCheck::OIL_CLEARANCE, AssemblyCheck::CAM_CLEARANCE };
			plastigauge.resolution = 0.0125;
			plastigauge.sigma = 0.005;
			plastigauge.price = 15.0;
			plastigauge.hoursPerUse = 0.3;
			list.push_back(plastigauge);

			MeasuringTool feeler;
			feeler.name = "Feeler Gauge Set";
			feeler.description = "Square the ring in its bore and find the blade that just slides.";
			feeler.assemblyChecks = { AssemblyCheck::RING_GAP };
			feeler.resolution = 0.025;
			feeler.sigma = 0.004;
			feeler.price = 25.0;
			feeler.hoursPerUse = 0.1;
			list.push_back(feeler);

			MeasuringTool dialIndicator;
			dialIndicator.name = "Dial Indicator & Deck Bridge";
			dialIndicator.description = "Find TDC and read how far the piston sits below the deck.";
			dialIndicator.assemblyChecks = { AssemblyCheck::DECK_CLEARANCE };
			dialIndicator.resolution = 0.01;
			dialIndicator.sigma = 0.004;
			dialIndicator.price = 120.0;
			dialIndicator.hoursPerUse = 0.2;
			list.push_back(dialIndicator);

			MeasuringTool heightMic;
			heightMic.name = "Installed Height Micrometer";
			heightMic.description = "Sits on the spring seat under a closed valve's retainer.";
			heightMic.assemblyChecks = { AssemblyCheck::INSTALLED_HEIGHT };
			heightMic.resolution = 0.01;
			heightMic.sigma = 0.008;
			heightMic.price = 90.0;
			heightMic.hoursPerUse = 0.1;
			list.push_back(heightMic);

			return list;
		}();
		return tools;
	}
}
