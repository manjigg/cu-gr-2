// 文件功能：提供高精度秒表的启动、停止、重置及耗时统计。
// 【零基础阅读】秒表记录开始时刻，结束时减开始时刻得到耗时。
// start/stop/reset 改计时状态，elapsed 一类查询读累计时间，不影响任务结果。
// 静态平台时钟细节是实现手段，先读公开方法就能理解如何测一个步骤的时间。
/* Copyright 2014-2018 Rsyn
 * clsRunning 是否正在计时，clsElapsedTime 已累计秒数；时刻与持续时间不是同一种量。
 * start/stop 切状态并更新累计，reset 清累计；系统时钟中间变量仅用于平台时间换算。
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
#ifndef STOPWATCH_H
#define STOPWATCH_H

// UFRGS - Guilherme Flach
// This class was based on http://pocoproject.org/.

#if _WIN32
#include <ctime>
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/times.h>
#endif

#include <cmath>
#include <sstream>
using std::ostringstream;
#include <string>
using std::string;
#include <iomanip>
using std::setfill;
using std::setw;

class Stopwatch {
	private:

		#if _WIN32

		#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
		  #define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64
		#else
		  #define DELTA_EPOCH_IN_MICROSECS  11644473600000000ULL
		#endif

		struct timezone {
		  int  tz_minuteswest; /* minutes W of Greenwich */
		  int  tz_dsttime;     /* type of dst correction */
		};

		inline int gettimeofday(struct timeval *tv, struct timezone *tz) {
		  FILETIME ft;
		  unsigned __int64 tmpres = 0;
		  static int tzflag;

		  if (NULL != tv) {
			GetSystemTimeAsFileTime(&ft);

			tmpres |= ft.dwHighDateTime;
			tmpres <<= 32;
			tmpres |= ft.dwLowDateTime;

			/*converting file time to unix epoch*/
			tmpres -= DELTA_EPOCH_IN_MICROSECS;
			tmpres /= 10;  /*convert into microseconds*/
			tv->tv_sec = (long)(tmpres / 1000000UL);
			tv->tv_usec = (long)(tmpres % 1000000UL);
		  }

		  if (NULL != tz) {
			if (!tzflag) {
			  _tzset();
			  tzflag++;
			}
			tz->tz_minuteswest = _timezone / 60;
			tz->tz_dsttime = _daylight;
		  }

		  return 0;
		} // end function

		#endif

		bool clsRunning;
		
		double clsElapsedTime;
		timeval clsCheckpoint;
		
		void update() {
			gettimeofday( &clsCheckpoint, NULL );
		} // end method
		
		double computeElapsedTimeWithRespectToCurrentTime() const {
			timeval t;
			gettimeofday( &t, NULL );
			
			return 
				( t.tv_sec  - clsCheckpoint.tv_sec  ) + 
				( t.tv_usec - clsCheckpoint.tv_usec )/1000000.0;
		} // end method
		
	public:
	
		Stopwatch() { reset(); }

		void reset() {
			clsElapsedTime = 0;
			clsRunning = false;
		} // end method

		void start() {
			clsRunning = true;
			update();
		} // end method

		void restart() {
			reset();
			start();
		} // end method

		void stop() {
			clsRunning = false;
			clsElapsedTime += computeElapsedTimeWithRespectToCurrentTime();
		} // end method

		double getElapsedTime() const {
			if ( clsRunning )
				return computeElapsedTimeWithRespectToCurrentTime();
			else
				return clsElapsedTime; 
		} // end method
		
		string getFormattedTime() const {
			const double hours = getElapsedTime()/(60*60);
			const double minutes = (hours - floor(hours)) * 60;
			const double seconds = (minutes - floor(minutes)) * 60;
			
			const int hh = (int) hours;
			const int mm = (int) minutes;
			const int ss = (int) seconds;
			
			ostringstream oss;
			oss << setfill('0') << hh << ":" << setw(2) << mm << ":" << setw(2) << ss;
			
			return oss.str();
		} // end method
		
		bool isRunning() const { return clsRunning; }
		
}; // end class

// -----------------------------------------------------------------------------

class StopwatchGuard {
private:
	Stopwatch &clsStopwatch;
public:
	StopwatchGuard(Stopwatch &stopwatch) : clsStopwatch(stopwatch) {clsStopwatch.start(); }
	~StopwatchGuard() { clsStopwatch.stop(); }
}; // end class

#endif
