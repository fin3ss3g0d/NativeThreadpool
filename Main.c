#include "Header.h"

volatile LONG shouldCancel = 0;

// Timer callback prototype might vary
void CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Parameter, PTP_TIMER Timer) {
	printf("Timer callback is executing.\n");

	InterlockedExchange(&shouldCancel, 1); // Signal cancellation
}

void CALLBACK WorkItemCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work) {
	printf("Work callback is executing.\n");

	while (InterlockedCompareExchange(&shouldCancel, 0, 0) == 0) {
		// Perform work here in small, interruptible chunks
		Sleep(1000);

		// Check for cancellation signal
		if (InterlockedCompareExchange(&shouldCancel, 0, 0) != 0) {
			// Perform cleanup and exit
			printf("Work callback is cancelled. Timeout reached.\n");
			break;
		}
	}
}

VOID CALLBACK WaitCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WAIT Wait, TP_WAIT_RESULT WaitResult) {
	printf("Wait callback is executing.\n");
}

void TestThreadpoolCallbackNative() {
	// Load the thread pool functions
	HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
	pTpAllocPool TpAllocPool = (pTpAllocPool)GetProcAddress(hNtDll, "TpAllocPool");
	pTpSetPoolMaxThreads TpSetPoolMaxThreads = (pTpSetPoolMaxThreads)GetProcAddress(hNtDll, "TpSetPoolMaxThreads");
	pTpSetPoolMinThreads TpSetPoolMinThreads = (pTpSetPoolMinThreads)GetProcAddress(hNtDll, "TpSetPoolMinThreads");
	pTpReleasePool TpReleasePool = (pTpReleasePool)GetProcAddress(hNtDll, "TpReleasePool");
	pTpWaitForWork TpWaitForWork = (pTpWaitForWork)GetProcAddress(hNtDll, "TpWaitForWork");
	pTpPostWork TpPostWork = (pTpPostWork)GetProcAddress(hNtDll, "TpPostWork");
	pTpReleaseWork TpReleaseWork = (pTpReleaseWork)GetProcAddress(hNtDll, "TpReleaseWork");
	pTpAllocWork TpAllocWork = (pTpAllocWork)GetProcAddress(hNtDll, "TpAllocWork");
	pTpAllocTimer TpAllocTimer = (pTpAllocTimer)GetProcAddress(hNtDll, "TpAllocTimer");
	pTpSetTimer TpSetTimer = (pTpSetTimer)GetProcAddress(hNtDll, "TpSetTimer");
	pTpReleaseTimer TpReleaseTimer = (pTpReleaseTimer)GetProcAddress(hNtDll, "TpReleaseTimer");
	pTpAllocWait TpAllocWait = (pTpAllocWait)GetProcAddress(hNtDll, "TpAllocWait");
	pTpSetWait TpSetWait = (pTpSetWait)GetProcAddress(hNtDll, "TpSetWait");
	pTpReleaseWait TpReleaseWait = (pTpReleaseWait)GetProcAddress(hNtDll, "TpReleaseWait");

	NTSTATUS status = 0;
	PTP_POOL pool = NULL;
	PTP_WORK work = NULL;
	PTP_TIMER timer = NULL;
	PTP_WAIT wait = NULL;
	TP_CALLBACK_ENVIRON pcbe;
	HANDLE waitEvent = NULL;

	// Set the maximum number of threads for the pool
	LONG maxThreads = 2;

	// Set the minimum number of threads for the pool
	LONG minThreads = 1;

	// Allocate a new thread pool
	status = TpAllocPool(&pool, NULL);
	if (!NT_SUCCESS(status)) {
		printf("TpAllocPool failed with status 0x%X\n", status);
		return;
	}

	/* 
	* Initialize the callback environment, inline function
	* https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-initializethreadpoolenvironment
	*/
	MyTpInitializeCallbackEnviron(&pcbe);

	/*
	* Set the pool to the callback environment, inline function
	* https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadpoolcallbackpool
	*/
	MyTpSetCallbackThreadpool(&pcbe, pool);

	// Set the minimum number of threads for the pool
	status = TpSetPoolMinThreads(pool, minThreads);
	if (!NT_SUCCESS(status)) {
		printf("TpSetPoolMinThreads failed with status 0x%X\n", status);
		return;
	}

	// Set the maximum number of threads for the pool
	status = TpSetPoolMaxThreads(pool, maxThreads);
	if (!NT_SUCCESS(status)) {
		printf("TpSetPoolMaxThreads failed with status 0x%X\n", status);
		return;
	}

	// Correctly allocate a work item with the callback and the callback environment
	status = TpAllocWork(&work, (PTP_WORK_CALLBACK)WorkItemCallback, NULL, &pcbe);
	if (!NT_SUCCESS(status)) {
		printf("TpAllocWork failed with status 0x%X\n", status);
		return;
	}
	
	// Post the work item to the thread pool
	status = TpPostWork(work);
	if (!NT_SUCCESS(status)) {
		printf("TpPostWork failed with status 0x%X\n", status);
		return;
	}
	
	// Allocate a timer
	status = TpAllocTimer(&timer, (PTP_TIMER_CALLBACK)TimerCallback, NULL, &pcbe);
	if (!NT_SUCCESS(status)) {
		printf("TpAllocTimer failed with status 0x%X\n", status);
		return;
	}

	// Set the timer to fire after 5 seconds
	LARGE_INTEGER dueTime;
	dueTime.QuadPart = (ULONGLONG)-(5 * 10 * 1000 * 1000);
	status = TpSetTimer(timer, &dueTime, 0, 0);
	if (!NT_SUCCESS(status)) {
		printf("TpSetTimer failed with status 0x%X\n", status);
		return;
	}

	/*
	* Sleep for 10 seconds, test the timer expiration
	* After five seconds, the timer callback will be executed
	* You would normally wait on something like an event and set it
	* in the work item callback to signal that the work is done
	*/
	Sleep(10000); // Wait for 10 seconds

	// Create an event to wait on
	waitEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (NULL == waitEvent) {
		printf("CreateEvent failed with LastError: %u\n", GetLastError());
		return;
	}

	// Allocate a wait object
	status = TpAllocWait(&wait, (PTP_WAIT_CALLBACK)WaitCallback, NULL, &pcbe);
	if (!NT_SUCCESS(status)) {
		printf("TpAllocWait failed with status 0x%X\n", status);
		return;
	}

	// Set the wait object to wait on the event
	// This example sets the wait object to wait indefinitely until the event is signaled
	TpSetWait(wait, waitEvent, NULL);

	// Simulate signaling the event after a delay (for demonstration)
	SetEvent(waitEvent);

	// Wait for an additional time to ensure callbacks can complete
	Sleep(5000); // Wait for another 5 seconds

	/*
	* Unused, it's best practice to use cooperative cancellation using timers.
	* This will wait for all work items to finish, if something doesn't finish
	* for whatever reason, it will wait indefinitely.
	*/
	/*
	status = TpWaitForWork(work, FALSE);
	if (!NT_SUCCESS(status)) {
		printf("TpWaitForWork failed with status 0x%X\n", status);
		return;
	}
	*/

	// Release the wait object when it is done
	status = TpReleaseWait(wait);
	if (!NT_SUCCESS(status)) {
		printf("TpReleaseWait failed with status 0x%X\n", status);
	}

	CloseHandle(waitEvent);

	// Release the timer when it is done
	status = TpReleaseTimer(timer);
	if (!NT_SUCCESS(status)) {
		printf("TpReleaseTimer failed with status 0x%X\n", status);
		return;
	}

	// Release the work item when it is done
	status = TpReleaseWork(work);
	if (!NT_SUCCESS(status)) {
		printf("TpReleaseWork failed with status 0x%X\n", status);
		return;
	}

	// Cleanup
	status = TpReleasePool(pool);
	if (!NT_SUCCESS(status)) {
		printf("TpReleasePool failed with status 0x%X\n", status);
		return;
	}
}

int main() {
	TestThreadpoolCallbackNative();
}
