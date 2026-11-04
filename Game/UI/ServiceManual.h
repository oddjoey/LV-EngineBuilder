#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct ImDrawList;
struct ImVec2;

// The LV-16 factory service manual, read as a book: two pages open at a time, turned with the
// arrow keys (or A and D) or the buttons under it. Every number in it comes from the engine's own
// spec, the part catalogue and the tool list, in the units the player plays in, so the book and
// the shop never disagree.
class ServiceManual
{
public:
	void Open();
	void Close();
	// Forward (positive) or back a pair of pages; true if it moved.
	bool Turn(int spreads);
	bool IsOpen() const;
	// Draws the open book; true when a page was turned this frame (for the sound).
	bool Draw(bool imperial);
	// Finds a shop model by name (0 if there's none), for the book's 3D views of tools and parts.
	void SetModelLookup(const std::function<uint32_t(const std::string&)>& lookup);
private:
	enum class Kind
	{
		HEADING,		// a section's title
		TEXT,
		STEP,			// numbered: value holds the number; views the parts it brings in, if any
		ITEM,			// a tool or machine: text its name, value its description, views its 3D view, specs its figures
		BULLET,
		SPEC,			// a dimension: text the name, value the figure
		NOTE,			// a boxed tip
		CONTENTS,		// a chapter in the contents: value holds its index
		FIGURE			// a diagram or graph: value holds its Figure, text its caption
	};
	// The book's diagrams and graphs, drawn from the engine's own figures (see ServiceManualFigures.cpp).
	enum class Figure
	{
		TOOL_RESOLUTION,
		BORE_POINTS,
		JOURNAL_POINTS,
		PISTON_CLEARANCE,
		RING_GAP,
		OIL_CLEARANCE,
		PLASTIGAUGE_SCALE,
		VALVE_DIMENSIONS,
		SPRING_CURVE,
		CAM_PROFILE,
		ASSEMBLY_FLOW,
		BELT_ROUTING,
		TORQUE_ACCURACY,
		HEAD_BOLT_SEQUENCE,
		MAIN_CAP_SEQUENCE,
		COMPRESSION_GRAPH
	};
	// A turning 3D view of one of the shop's models, with a caption under it.
	struct View
	{
		std::string model = "";
		std::string caption = "";
	};
	struct Line
	{
		Kind kind = Kind::TEXT;
		std::string text = "";
		std::string value = "";
		std::vector<View> views = {};
		std::vector<std::pair<std::string, std::string>> specs = {};	// name, figure
	};
	struct Chapter
	{
		std::string title = "";
		std::vector<Line> lines = {};
	};
	struct Page
	{
		int chapter = -1;		// -1: the cover
		std::vector<Line> lines = {};
	};

	void Build(bool imperial);
	void Paginate(float width, float height, float fontSize);
	float Measure(const Line& line, float width, float fontSize) const;
	// Draws one page; true if a chapter in the contents was clicked (the book has turned to it).
	bool DrawPage(ImDrawList* drawList, int page, const ImVec2& min, const ImVec2& max, float fontSize);
	void DrawCover(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float fontSize) const;
	void DrawFigure(ImDrawList* drawList, Figure figure, const ImVec2& min, const ImVec2& max, float fontSize) const;
	// How tall a figure is, in lines of text.
	static float FigureLines(Figure figure);
	// Lays a step's views out in rows under it; place (if given) is called with each view's index
	// and top left corner, relative to the row's start. Returns their height.
	float LayoutViews(const std::vector<View>& views, float width, float fontSize,
		const std::function<void(size_t, float, float)>& place = {}) const;
	// A view on its plate, the model turning inside it.
	void DrawView(ImDrawList* drawList, const std::string& model, const ImVec2& min, const ImVec2& max);
	int SpreadCount() const;

	bool					m_open = false;
	int						m_spread = 0;			// the open pair of pages: 2n on the left, 2n + 1 on the right
	bool					m_built = false;
	bool					m_builtImperial = false;
	float					m_pagedWidth = 0.f;		// what the pages were laid out for
	float					m_pagedHeight = 0.f;
	std::vector<Chapter>	m_chapters = {};
	std::vector<Page>		m_pages = {};
	std::vector<int>		m_chapterPage = {};		// each chapter's first page
	std::function<uint32_t(const std::string&)>	m_modelLookup = {};
	std::map<std::string, uint32_t>				m_models = {};	// looked up once each
};
