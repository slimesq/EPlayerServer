#include "Thread.h"

thread_local CThread* CThread::m_current = nullptr;
std::once_flag CThread::m_signalOnce;
