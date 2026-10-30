#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>

// A drop-down developer console, opened with the ` (tilde) key. The game registers the commands;
// the console parses the line, keeps the history and shows what they print.
class Console
{
public:
	using Arguments = std::vector<std::string>;
	using Handler = std::function<void(const Arguments& args)>;

	// usage is shown by help, e.g. "give <part> [count]".
	void AddCommand(const std::string& name, const std::string& usage, const std::string& help, Handler handler);

	void Toggle();
	void Close();
	bool IsOpen() const;

	void Print(const std::string& line);
	void Error(const std::string& line);
	void Clear();
	void Execute(const std::string& line);

	void Draw();
private:
	struct Command
	{
		std::string usage = "";
		std::string help = "";
		Handler handler = nullptr;
	};
	struct Line
	{
		std::string text = "";
		ImU32 color = 0;
	};

	static int InputCallback(ImGuiInputTextCallbackData* data);
	int OnInput(ImGuiInputTextCallbackData* data);
	void Add(const std::string& text, ImU32 color);

	std::map<std::string, Command>	m_commands = {};
	std::vector<Line>				m_lines = {};
	std::vector<std::string>		m_history = {};
	int								m_historyPosition = -1;	// -1: a new line, not one from the history
	char							m_input[256] = {};
	bool							m_open = false;
	bool							m_focusInput = false;
	bool							m_scrollToBottom = false;
};
