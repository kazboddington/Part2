#include <chrono>
#include <mutex>
#include <ctime>
#include <list>
#include <iostream>

#include "TimerManager.h"
typedef std::chrono::high_resolution_clock Clock;

/* Managaing tasks involves continuously sleep and move through the deltaList */
/* We call the callback when a deltaList item expires        */
void TimerManager::manageTasks()
{
	Clock::time_point t1 = Clock::now();
	while(true){
		const struct timespec waitime = {0, 1000000};
		struct timespec remaingTime;
		nanosleep(&waitime, &remaingTime);			
		Clock::time_point t2 = Clock::now();	
		std::chrono::duration<double> timePassed = 
			std::chrono::duration_cast<std::chrono::duration<double>>(t2-t1);
		//std::cout << "time passed: " << timePassed.count() << "seconds" << std::endl;
		deltaListMutex.lock();
		while(deltaList.size() > 0 && timePassed > std::chrono::duration<double>::zero()){
			deltaList.front().timeRemaining -= timePassed;	

			if(deltaList.front().timeRemaining < std::chrono::duration<double>::zero()){
				deltaListMutex.unlock();
				deltaList.front().callback();
				deltaListMutex.lock();
				timePassed = deltaList.front().timeRemaining;
				if (timePassed.count() < 0) timePassed = -timePassed;
				deltaList.pop_front();
			}else{
				break;
			}	
		}
		deltaListMutex.unlock();
		t1 = t2;
	}
}

void TimerManager::addTask(std::chrono::duration<double> delay, std::function<void()> callback)
{	

	std::cout << "adding task..." << std::endl;	
	std::lock_guard<std::mutex> deltaListLockGuard(deltaListMutex);	

	//Case that it's the 11st item
	if(deltaList.size() == 0){
		deltaElement newElement{delay, callback};
		deltaList.push_front(newElement);
		std::cout << "Packet added to empyy deltaList. Calling in " << delay.count() << " seconds" << std::endl;
		return;
	}

	//case that there are already items in the list
	for(std::list<deltaElement>::iterator it = deltaList.begin(); it != deltaList.end(); ++it){
		delay -= it->timeRemaining;	
		if (delay < std::chrono::duration<double>::zero()){
			// Got to the place in the list that the element needs to go.
			delay += it->timeRemaining;
			it->timeRemaining -= delay;
			deltaElement newElement{delay, callback};
			std::cout << "inserting element" << std::endl;
			deltaList.insert(it, newElement); 
			std::cout << "Adding new task. Will be called in " << delay.count() << " seconds." << std::endl;
			return;
		}
	}
	// If we get here, then our item has the longest delay, so we add it to the back
	deltaList.push_back(deltaElement{delay, callback});	
}
