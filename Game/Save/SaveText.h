#pragma once

#include <cctype>
#include <format>
#include <sstream>
#include <string>
#include <vector>

// The plain text the save file is written in, also used for the shop's state sent over the network.

// Writes whitespace-separated tokens; strings are quoted so they can hold spaces.
class Writer
{
public:
	Writer& Word(const std::string& word) { m_out << word << ' '; return *this; }
	Writer& Num(double value) { m_out << std::format("{}", value) << ' '; return *this; }
	Writer& Int(long long value) { m_out << value << ' '; return *this; }
	Writer& Str(const std::string& text)
	{
		m_out << '"';
		for (char c : text)
		{
			if (c == '"' || c == '\\')
				m_out << '\\' << c;
			else if (c == '\n')
				m_out << "\\n";
			else
				m_out << c;
		}
		m_out << "\" ";
		return *this;
	}
	Writer& Line() { m_out << '\n'; return *this; }
	std::string Text() const { return m_out.str(); }
private:
	std::ostringstream m_out;
};

// Reads the tokens back. Any mismatch marks the reader failed and every later read returns zeros,
// so parsing code can read straight through and check once at the end.
class Reader
{
public:
	explicit Reader(const std::string& text)
	{
		size_t i = 0;
		while (i < text.size())
		{
			if (std::isspace(static_cast<unsigned char>(text[i])))
			{
				i++;
				continue;
			}
			std::string token;
			if (text[i] == '"')
			{
				i++;
				while (i < text.size() && text[i] != '"')
				{
					if (text[i] == '\\' && i + 1 < text.size())
					{
						i++;
						token += text[i] == 'n' ? '\n' : text[i];
					}
					else
						token += text[i];
					i++;
				}
				i++;
			}
			else
				while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
					token += text[i++];
			m_tokens.push_back(token);
		}
	}

	std::string Next()
	{
		if (m_failed || m_position >= m_tokens.size())
		{
			Fail("the file ends early");
			return "";
		}
		return m_tokens[m_position++];
	}
	void Expect(const std::string& word)
	{
		auto token = Next();
		if (token != word)
			Fail(std::format("expected '{}' but found '{}'", word, token));
	}
	double Num()
	{
		auto token = Next();
		try { return m_failed ? 0.0 : std::stod(token); }
		catch (...) { Fail(std::format("'{}' isn't a number", token)); return 0.0; }
	}
	long long Int()
	{
		auto token = Next();
		try { return m_failed ? 0 : std::stoll(token); }
		catch (...) { Fail(std::format("'{}' isn't a whole number", token)); return 0; }
	}
	// A count of following records, bounded so a damaged file can't ask for billions.
	size_t Count()
	{
		auto count = Int();
		if (count < 0 || count > 100000)
			Fail("a record count is out of range");
		return m_failed ? 0 : static_cast<size_t>(count);
	}
	template <typename Enum>
	Enum Enumerated(int count)
	{
		auto value = Int();
		if (value < 0 || value >= count)
			Fail("a value is out of range");
		return static_cast<Enum>(m_failed ? 0 : value);
	}
	bool AtEnd() const { return m_position >= m_tokens.size(); }
	void Fail(const std::string& why)
	{
		if (!m_failed)
			m_error = why;
		m_failed = true;
	}
	bool Failed() const { return m_failed; }
	const std::string& Error() const { return m_error; }
private:
	std::vector<std::string>	m_tokens = {};
	size_t						m_position = 0;
	bool						m_failed = false;
	std::string					m_error = "";
};
