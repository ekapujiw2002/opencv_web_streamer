/* 
 * File:   fps_counter.h
 * Author: EX4
 *
 * Created on January 14, 2017, 11:15 AM
 * Ref :
 * - http://stackoverflow.com/questions/22148826/measure-opencv-fps
 */

#ifndef FPS_COUNTER_H
#define	FPS_COUNTER_H

#include <sys/timeb.h>

#ifdef	__cplusplus
extern "C" {
#endif

    double FPS_CLOCK();
    double fps_avgdur(double newdur);
    double fps_avg();

    //    global var
    double _avgdur = 0;
    double _fpsstart = 0;
    double _avgfps = 0;
    double _fps1sec = 0;

    //    for windows based
#if defined(_MSC_VER) || defined(WIN32)  || defined(_WIN32) || defined(__WIN32__) \
    || defined(WIN64)    || defined(_WIN64) || defined(__WIN64__) 

#include <windows.h>
    bool _qpcInited = false;
    double PCFreq = 0.0;
    __int64 CounterStart = 0;

    void InitCounter();

    void InitCounter() {
        LARGE_INTEGER li;
        if (!QueryPerformanceFrequency(&li)) {
            std::cout << "QueryPerformanceFrequency failed!\n";
        }
        PCFreq = double(li.QuadPart) / 1000.0f;
        _qpcInited = true;
    }

    double FPS_CLOCK() {
        if (!_qpcInited) InitCounter();
        LARGE_INTEGER li;
        QueryPerformanceCounter(&li);
        return double(li.QuadPart) / PCFreq;
    }

#endif

    //    for *nix based
#if defined(unix)        || defined(__unix)      || defined(__unix__) \
    || defined(linux)       || defined(__linux)     || defined(__linux__) \
    || defined(sun)         || defined(__sun) \
    || defined(BSD)         || defined(__OpenBSD__) || defined(__NetBSD__) \
    || defined(__FreeBSD__) || defined __DragonFly__ \
    || defined(sgi)         || defined(__sgi) \
    || defined(__MACOSX__)  || defined(__APPLE__) \
    || defined(__CYGWIN__) 

    double FPS_CLOCK() {
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return (t.tv_sec * 1000)+(t.tv_nsec * 1e-6);
    }
#endif

    double fps_avgdur(double newdur) {
        _avgdur = 0.98 * _avgdur + 0.02 * newdur;
        return _avgdur;
    }

    double fps_avg() {
        if (FPS_CLOCK() - _fpsstart > 1000) {
            _fpsstart = FPS_CLOCK();
            _avgfps = 0.7 * _avgfps + 0.3 * _fps1sec;
            _fps1sec = 0;
        }
        _fps1sec++;
        return _avgfps;
    }


#ifdef	__cplusplus
}
#endif

#endif	/* FPS_COUNTER_H */

