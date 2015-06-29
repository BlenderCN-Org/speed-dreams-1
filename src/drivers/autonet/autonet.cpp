/***************************************************************************

file                 : autonet.cpp
created              : Sat Nov 13 23:19:31 EST 2010
copyright            : (C) 2002 Utsav

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

#include <tgf.h>
#include <track.h>
#include <car.h>
#include <raceman.h>
#include <robottools.h>
#include <robot.h>
#include <linalg_t.h>       // v2t<T>


#include "serial.h"
#include "timer.h"

typedef v2t<tdble> vec2;

static tTrack *curTrack;
static int serialfd;

static void initTrack(int index, tTrack* track, void *carHandle, void **carParmHandle, tSituation *s);
static void newrace(int index, tCarElt* car, tSituation *s);
static void drive(int index, tCarElt* car, tSituation *s);
static void endrace(int index, tCarElt *car, tSituation *s);
static void shutdown(int index);
static int  InitFuncPt(int index, void *pt);

static void followModeDrive(tCarElt* car, tSituation *s);
static bool checkFollowMode();
static vec2 getLeadingCarPosition(tCarElt * myCar, tSituation *s, tdble distThreshold);
static tdble getDistance(tCarElt* car1, tCarElt* car2, tdble distThreshold);


static tCarElt *car;
static float accel = 0;
static float brake[4] = {0};
static float clutch = 0;
static float angle = 0;
static int gear = 0;
static int followCmd = 0;

static bool followMode = false;
static int lastFollowModeCmd = 0;
static int followCarIdx = -1;

static timer_t timerid;
volatile static bool signalStopSendData = false;
volatile static bool serialDataStopped = true;

/*
 * Module entry point
 */
extern "C" int autonet(tModInfo *modInfo)
{
    memset(modInfo, 0, 10*sizeof(tModInfo));

    modInfo->name    = "autonet";		/* name of the module (short) */
    modInfo->desc    = "";	            /* description of the module (can be long) */
    modInfo->fctInit = InitFuncPt;		/* init function */
    modInfo->gfId    = ROB_IDENT;		/* supported framework version */
    modInfo->index   = 1;

    return 0;
}

/* Module interface initialization. */
static int InitFuncPt(int index, void *pt)
{
    tRobotItf *itf  = (tRobotItf *)pt;

    itf->rbNewTrack = initTrack; /* Give the robot the track view called */
                                 /* for every track change or new race */
    itf->rbNewRace  = newrace; 	 /* Start a new race */
    itf->rbDrive    = drive;	 /* Drive during race */
    itf->rbPitCmd   = NULL;
    itf->rbEndRace  = endrace;	 /* End of the current race */
    itf->rbShutdown = shutdown;	 /* Called before the module is unloaded */
    itf->index      = index; 	 /* Index used if multiple interfaces */
    return 0;
}

/* Called for every track change or new race. */
static void initTrack(int index, tTrack* track, void *carHandle, void **carParmHandle, tSituation *s)
{
    curTrack = track;
    *carParmHandle = NULL;
}

void serialData(int signum)
{
    uint8_t a[512]; // Large buffer to read in all pending bytes from serial
    uint8_t calcChkSum = 0;
    uint8_t b[11];
    int i;

    if(signalStopSendData)
    {
        serialDataStopped = true;
        return;
    }

    if(serialPortRead(serialfd, a, 1) == 1) {
        if(a[0] == 0xAA) {
            if(serialPortRead(serialfd, a, 1) == 1) {
                if(a[0] == 0xCC) {
                    calcChkSum = 0xAA ^ 0xCC;
                    if(serialPortRead(serialfd, a, 512) >= 10) {
                        for(i = 0; i < 9; i++) {
                            calcChkSum = calcChkSum ^ a[i];
                        }

                        if(calcChkSum == a[9]) {
                            accel = a[0]/100.0;
                            brake[0] = a[1]/100.0;
                            brake[1] = a[2]/100.0;
                            brake[2] = a[3]/100.0;
                            brake[3] = a[4]/100.0;
                            angle = (int8_t)a[5]/50.0;
                            gear = (int8_t)a[6];
                            clutch = a[7]/100.0;
                            lastFollowModeCmd = followCmd;
                            followCmd = a[8];
                        }
                    }
                }
            }
        }
    }
    b[0] = 0xAA;
    b[1] = 0xCC;
    b[2] = car->_speed_x;

    uint16_t rpm = car->_enginerpm;
    b[3] = (rpm & 0xFF00) >> 8;
    b[4] = rpm & 0xFF;

    b[5] = car->_wheelSpinVel(FRNT_LFT) * car->_wheelRadius(FRNT_LFT);
    b[6] = car->_wheelSpinVel(FRNT_RGT) * car->_wheelRadius(FRNT_RGT);
    b[7] = car->_wheelSpinVel(REAR_LFT) * car->_wheelRadius(REAR_LFT);
    b[8] = car->_wheelSpinVel(REAR_RGT) * car->_wheelRadius(REAR_RGT);

    b[9] = car->_yaw_rate * 10;

    calcChkSum = 0;
    for(i = 0; i <= 9; i++){
        calcChkSum = calcChkSum ^ b[i];
    }
    b[10] = calcChkSum;

    serialPortWrite(serialfd, b, 11);
}

/* Start a new race. */
static void newrace(int index, tCarElt* car_local, tSituation *s)
{
    serialfd = serialPortOpen("/dev/ttyUSB0", 115200);
    car = car_local;
    timerid = timerInit(serialData, 2000000);
    serialDataStopped = false;
    enableTimerSignal();
}

/* Drive during race. */
static void drive(int index, tCarElt* car, tSituation *s)
{
    memset((void *)&car->ctrl, 0, sizeof(tCarCtrl));

    if(checkFollowMode())
    {
        followModeDrive(car, s);
    }

    //printf("steer: %f, yaw_rate: %f\n", angle, car->_yaw_rate);
#if 0
    const float SC = 1.0;
    /* Auto-steer */
    angle = RtTrackSideTgAngleL(&(car->_trkPos)) - car->_yaw;
    NORM_PI_PI(angle); // put the angle back in the range from -PI to PI
    angle -= SC*car->_trkPos.toMiddle/car->_trkPos.seg->width;
    angle = angle/car->_steerLock;
    accel = sqrt((18.0 - car->_speed_x) / 7.5);
    gear = 1;
#endif
    // set the values
    car->_steerCmd = angle;
    car->_gearCmd = gear;
    car->_accelCmd = accel;

    // Individual brake commands for each wheel
    car->_singleWheelBrakeMode = 1;
    car->_brakeFLCmd = brake[0];
    car->_brakeFRCmd = brake[1];
    car->_brakeRLCmd = brake[2];
    car->_brakeRRCmd = brake[3];
    car->_brakeCmd = (brake[0] + brake[1] + brake[2] + brake[3])/4.0; // Just for display in TORCS
    car->_clutchCmd = clutch;
}

/* End of the current race */
static void endrace(int index, tCarElt *car, tSituation *s)
{
    printf("endRace\n");
}

/* Called before the module is unloaded */
static void shutdown(int index)
{
    signalStopSendData = true;
    while(!serialDataStopped);
    disableTimerSignal();
    timerEnd(timerid);
    serialPortClose(serialfd);
    printf("Done with shutdown\n");
}

static void followModeDrive(tCarElt *car, tSituation *s)
{
    // Get position of nearest opponent in front
    // via: - Sensor utility
    //      - situation data
    // If distance below certain threshold
    // Drive in that direction (set angle)
    // If position in previous frame is known:
    //     Calculate speed from old and new world positon
    //     Try to adjust accel and brake to match speed of opponent
    //     (Try to shift gear accordingly)
    // Save new world position in old position

    vec2 targetPos = getLeadingCarPosition(car, s, 50.0);
}

//Get the position of the leading car
static vec2 getLeadingCarPosition(tCarElt * myCar, tSituation* s, tdble distThreshold)
{
    vec2 leadPos = vec2(0.0, 0.0);
    vec2 myPos = vec2(myCar->_pos_Y, myCar->_pos_X);

    tdble minDist = FLT_MAX;

    // If we are currently following somebody
    if(followCarIdx != -1)
    {
        tCarElt* lead = s->cars[followCarIdx];

        tdble distance = getDistance(lead, myCar, distThreshold);
        // If distance is to far, stop following that car
        if(distance < distThreshold)
        {
            followCarIdx = -1;
        }
        else
        {
            leadPos = vec2(lead->_pos_X, lead->_pos_Y);
        }
    }

    // If not following somebody
    if(followCarIdx == -1)
    {
        for(int i = 0; i < s->_ncars; i++)
        {
            tCarElt* lead = s->cars[i]; //possibly leading car

            if(lead == myCar) continue; //if own car

            tdble distance = getDistance(lead, myCar, distThreshold);

            // If distance is negative, we are in front of car (not possible to follow)
            if(distance < 0.0) continue;

            // If distance is larger than our follow mode distance threshold
            if(distance > distThreshold) continue;

            vec2 lPos = vec2(lead->_pos_X, lead->_pos_Y);
            tdble lDist = myPos.dist(lPos);
            if(lDist < minDist)
            {
                // From now on we may follow this car
                followCarIdx = i;
                leadPos = lPos;
                minDist = lDist;
            }
        }
    }
    return leadPos;
}

// Returns signed distance between car1 and car2
// if car1 is in front of car2, returned distance will be positive
// otherwise, distance will be negative
static tdble getDistance(tCarElt* car1, tCarElt* car2, tdble distThreshold)
{
    tdble distance = car1->_distFromStartLine - car2->_distFromStartLine;
    // If opponent crossed the start line, but we did not (assuming we are close behind the opponent)
    if (car1->_distFromStartLine < distThreshold && car2->_distFromStartLine > curTrack->length - distThreshold)
    {
        distance = (car1->_distFromStartLine + curTrack->length) - car2->_distFromStartLine;
    }
    return distance;
}

// Checks values of the transferred followMode signals and switches the followMode if the signal switches from enabled to disabled
// Compares to the behavior when releasing a keyboard button
static bool checkFollowMode()
{
    if(lastFollowModeCmd == 1 && followCmd == 0) // follow mode signal is disabled
    {
        followMode = !followMode; // switch follow mode
    }
    return followMode;
}