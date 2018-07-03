#pragma once

class ProcessorUsage
{
public:

	ProcessorUsage()
	{
		GetSystemTimes((LPFILETIME)&s_idleTime, (LPFILETIME)&s_kernelTime, (LPFILETIME)&s_userTime);
	}
	ProcessorUsage::~ProcessorUsage() {}

	// Return a value between 0-256, where 0 is 0% cpu usage and 256 is 100% usage
	int GetUsage()
	{
		__int64 idleTime;
		__int64 kernelTime;
		__int64 userTime;

		GetSystemTimes((LPFILETIME)&idleTime, (LPFILETIME)&kernelTime, (LPFILETIME)&userTime);

		int cpu;

		__int64 usr = userTime - s_userTime;
		__int64 ker = kernelTime - s_kernelTime;
		__int64 idl = idleTime - s_idleTime;
		__int64 sys = (usr + ker);

		if (sys)
			cpu = int((sys - idl) * 256 / sys); // System Idle take 100 % of cpu;
		else
			cpu = 0;

		s_idleTime = idleTime;
		s_kernelTime = kernelTime;
		s_userTime = userTime;

		return cpu;
	}

private:

	__int64 s_idleTime;
	__int64 s_kernelTime;
	__int64 s_userTime;
};
