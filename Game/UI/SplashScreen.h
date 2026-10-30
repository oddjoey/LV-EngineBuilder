#pragma once

// The studio card shown as the game starts: a monkey with a wrench and "monkeykobo", drawn over
// everything else. It fades in, holds and fades out, or goes as soon as a key or button is pressed.
class SplashScreen
{
public:
	void Update(float deltaTime, bool skipPressed);
	bool IsShowing() const;
	void Draw() const;
private:
	float	m_time = 0.f;
	bool	m_skipped = false;
};
