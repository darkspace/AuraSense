#pragma once

#include <vector>

class Smoother
{
public:

	Smoother(int smooth)
	{
		m_index = 0;
		m_values.resize(smooth);
	}
	Smoother::~Smoother() {}

	// add another sample and return new average
	int AddValue(int value)
	{
		m_values[(m_index++) % m_values.size()] = value;

		int avg = 0;
		for (size_t i = 0; i < m_values.size(); i++)
			avg += m_values[i];

		return avg /= m_values.size();
	}

private:

	std::vector<int> m_values;
	int m_index;
};
