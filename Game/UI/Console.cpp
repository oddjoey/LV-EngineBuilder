#include "Console.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <sstream>

namespace
{
	const ImU32 TEXT = IM_COL32(220, 220, 220, 255);
	const ImU32 ECHO = IM_COL32(130, 170, 230, 255);
	const ImU32 ERROR_TEXT = IM_COL32(235, 110, 100, 255);
	constexpr size_t MAX_LINES = 500;

	std::string Lower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	// Words split on spaces; "double quotes" keep a name with spaces in one word.
	std::vector<std::string> Split(const std::string& line)
	{
		std::vector<std::string> words;
		std::string word;
		bool quoted = false;
		for (char c : line)
		{
			if (c == '"')
				quoted = !quoted;
			else if (c == ' ' && !quoted)
			{
				if (!word.empty())
					words.push_back(word);
				word.clear();
			}
			else
				word += c;
		}
		if (!word.empty())
			words.push_back(word);
		return words;
	}
}

void Console::AddCommand(const std::string& name, const std::string& usage, const std::string& help, Handler handler)
{
	m_commands[Lower(name)] = { usage, help, std::move(handler) };
}

void Console::Toggle()
{
	if (m_open)
		Close();
	else
	{
		m_open = true;
		m_focusInput = true;
		m_scrollToBottom = true;
		if (m_lines.empty())
			Print("Developer console. Type help for the commands, ` or Esc to close.");
	}
}

void Console::Close()
{
	m_open = false;
}

bool Console::IsOpen() const
{
	return m_open;
}

void Console::Add(const std::string& text, ImU32 color)
{
	// One entry per line of the text.
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line))
		m_lines.push_back({ line, color });
	if (m_lines.size() > MAX_LINES)
		m_lines.erase(m_lines.begin(), m_lines.begin() + (m_lines.size() - MAX_LINES));
	m_scrollToBottom = true;
}

void Console::Print(const std::string& line)
{
	Add(line, TEXT);
}

void Console::Error(const std::string& line)
{
	Add(line, ERROR_TEXT);
}

void Console::Clear()
{
	m_lines.clear();
}

void Console::Execute(const std::string& line)
{
	auto words = Split(line);
	if (words.empty())
		return;
	Add("> " + line, ECHO);
	if (m_history.empty() || m_history.back() != line)
		m_history.push_back(line);
	m_historyPosition = -1;

	std::string name = Lower(words[0]);
	if (name == "help")
	{
		if (words.size() > 1)
		{
			auto it = m_commands.find(Lower(words[1]));
			if (it == m_commands.end())
				Error(std::format("No command called {}.", words[1]));
			else
				Print(std::format("{}\n  {}", it->second.usage, it->second.help));
			return;
		}
		Print("Commands (help <command> for more):");
		for (const auto& [command, info] : m_commands)
			Print(std::format("  {:<34} {}", info.usage, info.help));
		Print("  clear                              Empties the console.");
		return;
	}
	if (name == "clear")
	{
		Clear();
		return;
	}

	auto it = m_commands.find(name);
	if (it == m_commands.end())
	{
		Error(std::format("Unknown command {}. Type help for the list.", words[0]));
		return;
	}
	words.erase(words.begin());
	it->second.handler(words);
}

int Console::InputCallback(ImGuiInputTextCallbackData* data)
{
	return static_cast<Console*>(data->UserData)->OnInput(data);
}

int Console::OnInput(ImGuiInputTextCallbackData* data)
{
	switch (data->EventFlag)
	{
		case ImGuiInputTextFlags_CallbackCharFilter:
			// The key that opens and closes the console never ends up in the line.
			return data->EventChar == '`' || data->EventChar == '~' ? 1 : 0;
		case ImGuiInputTextFlags_CallbackCompletion:
		{
			// Tab finishes the command name: the whole of it if only one fits, else the list.
			std::string typed = Lower(std::string(data->Buf, data->BufTextLen));
			if (typed.find(' ') != std::string::npos)
				break;
			std::vector<std::string> matches;
			for (const auto& [command, info] : m_commands)
				if (command.rfind(typed, 0) == 0)
					matches.push_back(command);
			for (const char* builtIn : { "help", "clear" })
				if (std::string(builtIn).rfind(typed, 0) == 0)
					matches.push_back(builtIn);
			if (matches.size() == 1)
			{
				data->DeleteChars(0, data->BufTextLen);
				data->InsertChars(0, (matches[0] + " ").c_str());
			}
			else if (matches.size() > 1)
			{
				std::string list;
				for (const auto& match : matches)
					list += match + "  ";
				Print(list);
			}
			break;
		}
		case ImGuiInputTextFlags_CallbackHistory:
		{
			// Up and Down step through what was typed before.
			if (m_history.empty())
				break;
			int previous = m_historyPosition;
			if (data->EventKey == ImGuiKey_UpArrow)
				m_historyPosition = m_historyPosition == -1 ? static_cast<int>(m_history.size()) - 1 : (std::max)(0, m_historyPosition - 1);
			else if (data->EventKey == ImGuiKey_DownArrow && m_historyPosition != -1)
				m_historyPosition = m_historyPosition + 1 >= static_cast<int>(m_history.size()) ? -1 : m_historyPosition + 1;
			if (previous != m_historyPosition)
			{
				data->DeleteChars(0, data->BufTextLen);
				if (m_historyPosition != -1)
					data->InsertChars(0, m_history[m_historyPosition].c_str());
			}
			break;
		}
		default:
			break;
	}
	return 0;
}

void Console::Draw()
{
	if (!m_open)
		return;

	// Dropped down from the top of the screen, across its full width.
	ImVec2 display = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos({ 0.f, 0.f });
	ImGui::SetNextWindowSize({ display.x, display.y * 0.42f });
	ImGui::SetNextWindowBgAlpha(0.92f);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
	if (ImGui::Begin("Console", nullptr, flags))
	{
		float inputHeight = ImGui::GetFrameHeightWithSpacing();
		if (ImGui::BeginChild("ConsoleLog", { 0.f, -inputHeight }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			for (const auto& line : m_lines)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, line.color);
				ImGui::TextUnformatted(line.text.c_str());
				ImGui::PopStyleColor();
			}
			if (m_scrollToBottom)
				ImGui::SetScrollHereY(1.f);
			m_scrollToBottom = false;
		}
		ImGui::EndChild();

		ImGui::SetNextItemWidth(-1.f);
		ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter
			| ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
		if (m_focusInput)
		{
			ImGui::SetKeyboardFocusHere();
			m_focusInput = false;
		}
		if (ImGui::InputText("##ConsoleInput", m_input, sizeof(m_input), inputFlags, &Console::InputCallback, this))
		{
			std::string line = m_input;
			m_input[0] = '\0';
			Execute(line);
			// Keep typing without clicking back into the box.
			m_focusInput = true;
		}
	}
	ImGui::End();
}
