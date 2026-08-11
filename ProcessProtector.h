#pragma once
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

class ProcessProtector
{
public:
	static bool DenyTerminateAccess();
};

