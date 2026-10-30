#pragma once

// Developer switches, opened with F3. None of this is saved; it's for working on the game.
struct DevSettings
{
	bool xray = false;			// the parts' true geometry next to the player's measurements
	bool showBounds = false;	// wireframes of everything that collides or can be picked up
	float timeScale = 1.f;		// how much faster than normal the day goes by
	bool timePaused = false;	// the clock stands still
};

// One window for everything developer-facing: the game's switches, the time of day, and Lordi's
// own debug panels (performance, cameras, objects), which replace the engine's Home window.
class DevMenu
{
public:
	void Toggle();
	void Close();
	bool IsOpen() const;
	// timeOfDay (hours) is changed directly by the time controls.
	void Draw(float& timeOfDay);
	const DevSettings& GetSettings() const;
	// For the console, which can flip the same switches.
	DevSettings& EditSettings();
	// True once after "Assemble a complete engine" is clicked.
	bool TakeAssembleRequest();
private:
	void DrawGameTab(float& timeOfDay);

	bool		m_open = false;
	DevSettings	m_settings = {};
	bool		m_assembleRequested = false;
};
