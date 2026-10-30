#pragma once

#include <string>
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
private:
	enum class Kind
	{
		HEADING,		// a section's title
		TEXT,
		STEP,			// numbered: value holds the number
		BULLET,
		SPEC,			// a dimension: text the name, value the figure
		NOTE,			// a boxed tip
		CONTENTS		// a chapter in the contents: value holds its index
	};
	struct Line
	{
		Kind kind = Kind::TEXT;
		std::string text = "";
		std::string value = "";
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
};
