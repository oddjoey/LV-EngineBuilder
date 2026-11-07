#include "ServiceManual.h"

#include <map>

// What each figure in the service manual means, shown when the reader hovers over it: what is
// measured, where, and what happens when it's out. Looked up by the figure's name as the book prints
// it, for the spec tables and for the figures listed under each tool and machine.

namespace
{
	const std::map<std::string, const char*> TIPS = {
		// 1. General Information
		{ "Cylinders", "How many cylinders the engine has and how they're laid out. An inline four has all four in one straight row along the crank." },
		{ "Bore x stroke", "Bore is the cylinder's diameter; stroke is how far the piston travels from top to bottom. Together they set the displacement." },
		{ "Displacement", "The volume all the pistons sweep in one turn of the crank: bore area x stroke x cylinders. More displacement, more air, more power." },
		{ "Compression ratio", "How much the mixture is squeezed: the cylinder's volume with the piston at the bottom, divided by its volume at the top. Higher makes more power, until the engine starts to detonate." },
		{ "Main bearings", "The bearings the crankshaft turns in, in housings across the bottom of the block. More of them support the crank better." },
		{ "Camshaft", "SOHC: a single overhead camshaft in the head opens both valves of every cylinder. Its journals are the round bearing surfaces it turns on." },
		{ "Valves", "One intake valve to let the mixture in and one exhaust valve to let the burnt gas out, for each cylinder." },
		{ "Valvetrain", "How the cam opens the valves. Here each lobe pushes a rocker arm, which pivots on its shaft and presses its valve open." },
		{ "Timing drive", "What turns the cam in step with the crank. A toothed rubber belt; the cam turns once for every two turns of the crank." },
		{ "Fuel system", "How fuel gets into the air. A carburettor meters fuel into the air drawn through it, through two barrels (throats)." },
		{ "Ignition", "What fires the spark plugs. The distributor, turned by the cam, sends the spark to each plug in turn." },
		{ "Rated output", "The most power and torque a correctly built engine makes, and the rpm each comes at. The dyno measures what yours makes." },
		{ "Redline", "The highest safe engine speed. Past it the valves float and parts are stressed beyond what they're made for." },

		// 4. Cylinder Block
		{ "Bore diameter", "The cylinder's diameter, measured with a bore gauge. Bores wear bigger, most at the top of the ring travel. Past the range, bore it oversize and fit oversize pistons." },
		{ "Bore taper", "How much bigger the bore is at the top than at the bottom: the biggest difference between readings at different heights. Too much and the rings can't seal all the way down." },
		{ "Bore out of round", "How far the bore has worn oval: the biggest difference between readings across and along the block at one height. Rings can't seal an oval bore." },
		{ "Bore finish", "How rough the cylinder wall is, in micrometres Ra, read with the profilometer. Too smooth (glazed) and the rings never seat; too rough and they wear fast. Honing sets it." },
		{ "Main housing bore", "The diameter of the holes the main bearings sit in, with the caps torqued on. Out of size or out of line, they're line bored." },
		{ "Deck height", "The distance from the crank's centre line to the top face (deck) of the block. It sets where the piston sits at top dead centre, which sets quench and compression." },
		{ "Deck flatness", "How far the deck is from flat, read with a straightedge and feeler gauges. A warped deck won't seal the head gasket." },
		{ "Resurfacing limit", "The most metal that may ever be taken off the deck in total. Past it the block is too thin and compression too high: replace it." },

		// 5. Crankshaft
		{ "Main journal diameter", "The diameter of the crank's main journals, where it turns in the main bearings. Measured with an outside micrometer; worn small ones are ground undersize." },
		{ "Rod journal diameter", "The diameter of the crank pins the connecting rods run on. Measured with an outside micrometer; worn small ones are ground undersize." },
		{ "Journal taper", "The difference in a journal's diameter from one end to the other. A tapered journal loads one edge of its bearing." },
		{ "Journal out of round", "The difference in a journal's diameter at two angles 90 degrees apart. An oval journal hammers its bearing." },
		{ "Journal finish", "How rough a journal's surface is. A scored journal wears through its bearing: polish or grind it." },
		{ "Stroke", "How far each piston travels, twice the distance from the crank's centre to a crank pin's centre." },

		// 6. Rods, Pistons and Rings
		{ "Big end bore (housing)", "The diameter of the rod's big end, where its bearing shells sit, measured with the cap torqued on. Big ends stretch oval with use." },
		{ "Centre to centre length", "The rod's length from the centre of the big end to the centre of the small end. It sets how high the piston comes at top dead centre." },
		{ "Bend, per 100 mm", "How far the rod's two ends are out of parallel, side to side, measured on the rod aligner. A bent rod holds its piston crooked in the bore." },
		{ "Twist, per 100 mm", "How far the rod's two ends are twisted against each other, measured on the rod aligner. A twisted rod wears the piston and bearing on one edge." },
		{ "Side clearance on the crank pin", "The gap between the side of the rod's big end and the crank cheek beside it, measured with feelers. It lets oil out of the bearing." },
		{ "Side clearance limit", "The most side clearance a rod may have. Past it the rod slides about on the pin and knocks: replace it." },
		{ "Skirt diameter (standard)", "The piston's diameter across the skirt, the part below the rings. Bore diameter minus this is the piston to wall clearance." },
		{ "Compression height", "From the centre of the piston's pin hole to the top of the piston. It sets how high the piston comes at top dead centre." },
		{ "Dish volume", "The volume of the recess in the piston crown. A bigger dish lowers the compression ratio." },
		{ "Cast", "Piston to wall clearance for a cast aluminium piston: bore diameter minus skirt diameter. Cast pistons grow little when hot." },
		{ "Hypereutectic", "Piston to wall clearance for a hypereutectic (high silicon) piston. It grows a little more than a cast one when hot." },
		{ "Forged 2618", "Piston to wall clearance for a forged 2618 alloy piston. Forged pistons grow the most when hot, so they need the most room cold." },
		{ "Top ring end gap", "The gap between the ring's ends, squared in its own bore. Too tight and the ends butt when hot and break the ring; too wide and compression leaks past." },

		// 7. Bearings and Oil Clearance
		{ "Main shell thickness (std)", "The wall thickness of a standard main bearing shell. Undersize shells are thicker, to suit a journal ground smaller." },
		{ "Rod shell thickness (std)", "The wall thickness of a standard rod bearing shell. Undersize shells are thicker, to suit a journal ground smaller." },
		{ "Main oil clearance", "The gap the oil film fills between a main journal and its bearing: housing bore minus journal minus both shells. Check it with Plastigauge." },
		{ "Rod oil clearance", "The gap the oil film fills between a rod journal and its bearing: big end bore minus journal minus both shells. Check it with Plastigauge." },

		// 8. Cylinder Head and Valves
		{ "Combustion chamber volume", "The volume of the chamber in the head above each cylinder, measured with the burette and cc plate. Surfacing the head makes it smaller and raises compression." },
		{ "Head gasket face flatness", "How far the head's gasket face is from flat, read with a straightedge and feelers. Warped, it won't seal the gasket." },
		{ "Manifold face flatness", "How far the faces the intake and exhaust manifolds bolt to are from flat. Warped, the manifold gaskets leak." },
		{ "Resurfacing limit (gasket face)", "The most metal that may ever come off the head's gasket face in total. Past it the head is too thin and compression too high: replace it." },
		{ "Intake manifold flange flatness", "How far the intake manifold's own flange is from flat. A warped flange leaks air and leans the mixture." },
		{ "Head gasket thickness (compressed)", "How thick the head gasket is once crushed by the torqued head bolts. It adds to the quench and the compressed volume." },
		{ "Head gasket bore", "The diameter of the gasket's holes over the cylinders. It counts towards the volume above the piston." },
		{ "Guide bore", "The inside diameter of a valve guide, the tube the valve stem slides in. Measured with a small hole or small-bore gauge." },
		{ "Guide taper (bellmouth)", "How much bigger the guide is at its ends than in the middle. Guides wear bellmouthed where the stem rocks." },
		{ "Guide out of round", "How far the guide has worn oval: the difference across it at two angles." },
		{ "Intake stem diameter", "The diameter of the intake valve's stem, measured with a micrometer. Stems wear thinner where they ride in the guide." },
		{ "Exhaust stem diameter", "The diameter of the exhaust valve's stem, measured with a micrometer. Stems wear thinner where they ride in the guide." },
		{ "Intake stem to guide", "Guide bore minus intake stem diameter. Too tight and the hot stem sticks; too loose and the valve rocks and burns oil." },
		{ "Exhaust stem to guide", "Guide bore minus exhaust stem diameter. Exhaust valves run hotter and grow more, so they get a little more." },
		{ "Intake stem to guide, limit", "The most intake stem clearance allowed. Past it, ream the guide oversize and fit an oversize-stem valve." },
		{ "Exhaust stem to guide, limit", "The most exhaust stem clearance allowed. Past it, ream the guide oversize and fit an oversize-stem valve." },
		{ "Seat width", "How wide the band is where the valve touches its seat. Too narrow and the valve can't shed heat; too wide and it doesn't seal well." },
		{ "Seat runout", "How far the seat is off centre from the guide, as the valve would see it turning. A seat that runs out won't seal." },
		{ "Valve runout", "How far the valve's face wobbles as it turns about its stem. A bent valve won't seal on its seat." },
		{ "Valve margin", "The thickness of the valve head's edge, between its face and its top. Refacing makes it thinner." },
		{ "Valve margin minimum", "The thinnest a valve's margin may be. Thinner than this, the edge overheats and burns: replace the valve." },
		{ "Overall length", "The valve's length, tip to head." },
		{ "Free length", "The spring's length standing on the bench with no load on it. A spring that has sagged short is weak." },
		{ "Out of square", "How far the top of the spring leans when it's stood against a square. A leaning spring pushes its valve stem sideways and wears the guide." },
		{ "Installed height", "How tall the spring is when fitted, from its seat in the head to the underside of the retainer. It sets how hard the spring holds the valve shut." },
		{ "Tension at installed height", "How hard a new spring pushes when squeezed to its installed height, read on the spring tester." },
		{ "Minimum tension", "The weakest a spring may push at installed height. Weaker, the valves float at high rpm: replace it." },
		{ "Spring rate", "How much harder the spring pushes for every millimetre it's squeezed." },
		{ "Installed height, in the head", "The installed height measured in the head with the valve and retainer fitted. Cutting seats and refacing valves raises it and weakens the spring." },

		// 9. Camshaft
		{ "Cam bearing bore (head)", "The diameter of the cam's bearing bores in the head, with the cam caps torqued on." },
		{ "Cam journal diameter", "The diameter of the cam's journals, measured with a micrometer." },
		{ "Lobe height", "The height of a cam lobe measured across its nose. It sets how far the valve opens." },
		{ "Lobe height minimum", "The lowest a lobe may be worn. Below it the valve doesn't open fully and the cylinder loses power: replace the cam." },
		{ "Cam oil clearance", "The gap the oil fills between a cam journal and its bore: bore minus journal. Check it with Plastigauge." },
		{ "Cam oil clearance limit", "The most cam oil clearance allowed. Past it, replace the cam and the head." },
		{ "End play", "How far the cam slides back and forth along its length in the head, read with a dial indicator on its nose." },
		{ "End play limit", "The most end play allowed. Past it, replace the cam and/or the head." },
		{ "Bend (runout)", "How far the cam's centre journal wobbles when the cam is turned in V-blocks. A bent cam binds in its bearings." },
		{ "Fuel pump eccentric height", "The height of the off-centre lobe on the cam that works the mechanical fuel pump's arm." },
		{ "Fuel pump eccentric minimum", "The lowest the fuel pump eccentric may be worn. Below it, the pump short-strokes and starves the engine at high rpm." },
		{ "Rocker arm bore", "The diameter of the hole through a rocker arm that it pivots on its shaft by." },
		{ "Rocker shaft diameter", "The diameter of a rocker shaft where the arms ride. Shafts wear under each arm." },
		{ "Arm to shaft clearance", "Rocker arm bore minus shaft diameter: how loosely the arm pivots." },
		{ "Arm to shaft clearance, limit", "The most arm to shaft clearance allowed. Past it, the arm rocks on its shaft and ticks: replace the arm, the shaft or both." },

		// 12. Final Checks and the Dyno
		{ "Deck height (block)", "The distance from the crank's centre line to the block's deck. It sets how far below the deck the piston stops." },
		{ "Quench (piston to head)", "The gap between the piston crown and the flat of the head at top dead centre: deck clearance plus the compressed gasket. It squeezes the mixture into the chamber for a clean burn." },
		{ "Target", "The compression ratio the finished engine should come out at. Too high and it detonates; too low and it's down on power." },

		// Under each tool and machine
		{ "Reads to", "The smallest step the tool can show. A tool can't tell apart two sizes closer together than this." },
		{ "Repeatability", "How far the same measurement can scatter from one try to the next. Measure again if a reading looks wrong." },
		{ "Fits", "The range of sizes the tool can measure." },
		{ "Sizes", "The fastener sizes the set has a socket or wrench for." },
		{ "Accuracy", "How far the torque the wrench actually reaches can be from the torque it's set to." },
		{ "Up to", "The most torque the tool can apply." },
		{ "Time per bolt", "How long the mouse button is held to run a typical nut or bolt in or out with the tool. Bigger ones take a little longer." },
		{ "Battery use", "How much of a battery pack's charge each nut or bolt takes. Tight ones take more, the crank pulley bolt most of all." },
		{ "Holds", "How much charge the pack holds, in amp hours: the more, the more nuts and bolts between charges." },
		{ "Charges at", "The current the charger puts into a pack, in amps. One amp puts back one amp hour a minute." },
		{ "Timing belt deflection (2 kg)", "How far the timing belt's tight side gives when pushed with 2 kg halfway between the pulleys. More is slack (the idler spring is tired or the belt stretched), less is too tight." },
		{ "Fan belt deflection, new (10 kg)", "How far a new fan belt gives when pushed with 10 kg halfway between the water pump and the alternator. Set it by swinging the alternator out on its pivot before bolting it." },
		{ "Fan belt deflection, used (10 kg)", "The same for a belt that's already run in: it's stretched a little, so it's allowed to give more." },
		{ "Fan belt size", "The fan belt's width and length: an 11 mm V-belt, 980 mm round." },
		{ "Holds size to", "How close to the target size the machine cuts, every time." },
		{ "Finish", "How rough the surface the machine leaves is, in micrometres Ra." },
		{ "Most per setup", "The most metal the machine can take off in one setup." },
	};
}

const char* ServiceManual::SpecTip(const std::string& name)
{
	auto it = TIPS.find(name);
	return it == TIPS.end() ? nullptr : it->second;
}
