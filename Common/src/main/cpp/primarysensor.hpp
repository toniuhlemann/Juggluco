/*      This file is part of Juggluco, an Android app to receive and display         */
/*      glucose values from Freestyle Libre 2 and 3 sensors.                         */
/*                                                                                   */
/*      Juggluco is free software: you can redistribute it and/or modify             */
/*      it under the terms of the GNU General Public License as published            */
/*      by the Free Software Foundation, either version 3 of the License, or         */
/*      (at your option) any later version.                                          */
/*                                                                                   */
/*      Juggluco is distributed in the hope that it will be useful, but              */
/*      WITHOUT ANY WARRANTY; without even the implied warranty of                   */
/*      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                         */
/*      See the GNU General Public License for more details.                         */

#pragma once
#include <mutex>
#include <vector>
#include <algorithm>
#include <string.h>
#include <time.h>
#include "sensoren.hpp"
#include "settings/settings.hpp"

extern Sensoren *sensors;

/* Primary sensor routing.
 *
 * Exactly one persistent primary sensor, identified by its 16-char sensor name
 * stored in settings (Tings::primarysensor, mmap-backed, so it survives process
 * restarts and reboots). Only values of the primary sensor may reach live
 * outputs (xDrip/glucodata broadcasts, widget, web server current values,
 * Nightscout upload). All sensors keep being received, stored and diagnosed.
 *
 * A sensor stays primary until it is finished per the existing lifecycle
 * (sensor.finished: set by finishSensor or by checkinfo once the sensor is
 * regularly expired without recent data) or until it is removed from the
 * sensor list. A mere bluetooth/data gap never triggers a handover.
 * On handover the alive sensor with the smallest starttime at or after the
 * old primary's starttime takes over; without a stored primary (migration)
 * the oldest alive sensor is chosen, so a running sensor beats a newly
 * activated one.
 */
namespace primarysensor {

inline std::mutex lock;

/* alive per Juggluco's own lifecycle: known, not finished. checkinfo() applies
 * the existing expiry rules and may set finished itself. */
inline bool alive(const int ind,const uint32_t nu) {
   if(!sensors||ind<0||ind>sensors->last())
      return false;
   if(!sensors->getsensor(ind)->name[0])
      return false;
   sensors->checkinfo(ind,nu);
   return !sensors->getsensor(ind)->finished;
   }

/* Resolves the primary index and persists an automatic handover.
 * Returns -1 when no primary can be resolved (no alive sensor). */
inline int resolvelocked(const uint32_t nu) {
   if(!sensors||!settings||!settings->data())
      return -1;
   char *stored=settings->data()->primarysensor;
   stored[sensornamelen]='\0';
   uint32_t oldstart=0;
   if(stored[0]) {
      if(const int ind=sensors->sensorindex(stored);ind>=0) {
         if(alive(ind,nu))
            return ind;
         oldstart=sensors->getsensor(ind)->starttime;
         }
      }
   int best=-1,fallback=-1;
   uint32_t beststart=0,fallbackstart=0;
   const int last=sensors->last();
   for(int i=0;i<=last;++i) {
      if(!alive(i,nu))
         continue;
      const uint32_t st=sensors->getsensor(i)->starttime;
      if(oldstart&&st>=oldstart&&(best<0||st<beststart)) {
         best=i;
         beststart=st;
         }
      if(fallback<0||st<fallbackstart) {
         fallback=i;
         fallbackstart=st;
         }
      }
   const int take=best>=0?best:fallback;
   if(take>=0) {
#ifdef LOGGER
      LOGGER("primarysensor handover to %d %s\n",take,sensors->getsensor(take)->name);
#endif
      memcpy(stored,sensors->getsensor(take)->name,sensornamelen);
      stored[sensornamelen]='\0';
      }
   return take;
   }

inline int index() {
   std::lock_guard<std::mutex> guard(lock);
   return resolvelocked(time(nullptr));
   }

/* A rival is an alive non-primary sensor while a primary exists. Rival values
 * never appear in outputs; finished sensors stay visible as history. */
inline bool rivalof(const int primary,const int ind) {
   if(primary<0||ind==primary)
      return false;
   return alive(ind,time(nullptr));
   }

inline bool rival(const int ind) {
   return rivalof(index(),ind);
   }

inline void removerivals(std::vector<int> &indices) {
   const int primary=index();
   if(primary<0)
      return;
   std::erase_if(indices,[primary](const int ind){return rivalof(primary,ind);});
   }

/* live outputs: allowed when no primary exists or ind is the primary */
inline bool allowlive(const int ind) {
   const int primary=index();
   return primary<0||primary==ind;
   }

/* manual, confirmed selection from the sensor dropdown */
inline void setprimaryindex(const int ind) {
   if(!sensors||!settings||!settings->data()||ind<0||ind>sensors->last())
      return;
   std::lock_guard<std::mutex> guard(lock);
   char *stored=settings->data()->primarysensor;
   memcpy(stored,sensors->getsensor(ind)->name,sensornamelen);
   stored[sensornamelen]='\0';
   }
}
