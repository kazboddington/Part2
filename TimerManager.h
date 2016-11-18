#ifndef TIMER_MANAGER
#define TIMER_MANAGER

#include <chrono>
#include <mutex>
#include <ctime>
#include <list>
#include <iostream>

#include "TimerManager.h"
typedef std::chrono::high_resolution_clock Clock;

class TimerManager
{	
	typedef struct deltaElement
	{
		std::chrono::duration<double> timeRemaining;
		std::function<void()> callback;
	}deltaElement;
 	std::mutex deltaListMutex;
	std::list<deltaElement> deltaList;
	Clock clk;
public:

	/* Managaing tasks involves continuously sleep and move through the deltaList */
	/* We call the callback when a deltaList item expires        */
	void manageTasks();

	void addTask(std::chrono::duration<double> delay, std::function<void()> callback);
};
#endif

