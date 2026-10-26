#pragma once

// Developer switches, opened with F3. None of this is saved; it's for working on the game.
struct DevSettings
{
	bool xray = false;			// the parts' true geometry next to the player's measurements
	bool showBounds = false;	// wireframes of everything that collides or can be picked up
};

class DevMenu
{
public:
	void Toggle();
	void Close();
	bool IsOpen() const;
	void Draw();
	const DevSettings& GetSettings() const;
private:
	bool		m_open = false;
	DevSettings	m_settings = {};
};
