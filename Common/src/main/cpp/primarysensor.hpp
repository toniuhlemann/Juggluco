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
#include <algorithm>
#include <span>
#include <string.h>
#include <time.h>
#include "sensoren.hpp"
#include "settings/settings.hpp"

extern Sensoren *sensors;

/* Primary sensor routing.
 *
 * Exactly one persistent primary sensor at a time. Its tenure is recorded in a
 * time-ordered epoch log (Tings::primaryepochs, mmap-backed, so it survives
 * process restarts and reboots). Only values of the sensor that was primary at
 * the value's own timestamp may reach live outputs (xDrip/glucodata broadcasts,
 * widget, web server, Nightscout upload). That makes the outputs one continuous
 * primary time series: the old primary contributes up to the handover moment,
 * the new one from the handover moment on, and values a sensor produced before
 * its tenure (warm-up) are never delivered later. All sensors keep being
 * received, stored and diagnosed; values older than primaryroutingstart predate
 * the routing and stay visible for every sensor.
 *
 * A sensor stays primary until it is finished per the existing lifecycle
 * (sensor.finished: set by finishSensor or by checkinfo once the sensor is
 * regularly expired without recent data) or until it is removed from the
 * sensor list. A mere bluetooth/data gap never triggers a handover.
 * On automatic handover the alive sensor with the smallest starttime at or
 * after the old primary's starttime takes over. The handover epoch is
 * backdated to just after the old primary's last stored value, so the
 * successor covers the gap the lifecycle detection needed to notice the end,
 * while everything the successor produced while the old primary still
 * delivered stays suppressed. Without any stored epoch (migration) the oldest
 * alive sensor is chosen; the routing then starts at its starttime, but never
 * before the end of already finished sensors, so previously served history
 * keeps its original sensors.
 *
 * All epoch state is read and written under one mutex; the *locked functions
 * must only be called with that mutex held. */
namespace primarysensor {

inline std::mutex lock;

/* how a tenure began; stored per epoch (Tings::primaryepoch::reason) */
enum epochreason : uint8_t {
   REASON_UNKNOWN=0,
   REASON_MIGRATION=1, //first resolution ever
   REASON_AUTO=2,      //old primary finished/expired/removed
   REASON_MANUAL=3,    //confirmed selection in the sensor dialog
   };

inline const char *reasonname(const uint8_t reason) {
   switch(reason) {
      case REASON_MIGRATION: return "migration";
      case REASON_AUTO: return "auto";
      case REASON_MANUAL: return "manual";
      default: return "unknown";
      }
   }

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

inline int epochcountlocked() {
   if(!settings||!settings->data())
      return 0;
   int32_t &nr=settings->data()->primaryepochnr;
   if(nr<0||nr>Tings::maxprimaryepochs) {
      //foreign or corrupt bytes: restart the routing cleanly instead of
      //interpreting garbage epochs (which would suppress valid history)
      nr=0;
      settings->data()->primaryroutingstart=0;
      }
   return nr;
   }

/* name of the currently stored primary (last epoch), nullptr when none chosen yet */
inline const char *storednamelocked() {
   const int nr=epochcountlocked();
   if(nr<=0)
      return nullptr;
   return settings->data()->primaryepochs[nr-1].name;
   }

/* keeps the epoch list sorted even across clock jumps; sets the routing anchor
 * on the very first epoch */
inline void appendepochlocked(const char *name16,uint32_t from,const uint8_t reason) {
   auto *dat=settings->data();
   int nr=epochcountlocked();
   if(nr==0)
      dat->primaryroutingstart=from;
   else {
      if(nr>=Tings::maxprimaryepochs) {
         //evicted tenures keep being suppressed: primaryroutingstart stays put,
         //and times in [primaryroutingstart,epochs[0].from) match no epoch
         memmove(&dat->primaryepochs[0],&dat->primaryepochs[1],
                 sizeof(Tings::primaryepoch)*(Tings::maxprimaryepochs-1));
         nr=Tings::maxprimaryepochs-1;
         }
      if(from<dat->primaryepochs[nr-1].from)
         from=dat->primaryepochs[nr-1].from;
      }
   auto &epoch=dat->primaryepochs[nr];
   epoch.from=from;
   memcpy(epoch.name,name16,sensornamelen);
   epoch.name[sensornamelen]='\0';
   epoch.reason=reason;
   dat->primaryepochnr=nr+1;
#ifdef LOGGER
   LOGGER("primarysensor epoch %d: %s from %u reason %d\n",nr,epoch.name,from,reason);
#endif
   }

/* Was this sensor the primary at time t? Values before the routing started are
 * allowed for every sensor. */
inline bool allowedatlocked(const char *name16,const uint32_t t) {
   const int nr=epochcountlocked();
   if(nr<=0)
      return true;
   const auto *dat=settings->data();
   if(t<dat->primaryroutingstart)
      return true;
   const auto *epochs=dat->primaryepochs;
   for(int i=nr-1;i>=0;--i) {
      if(epochs[i].from<=t)
         return !memcmp(epochs[i].name,name16,sensornamelen);
      }
   return false; //tenure record evicted: stays suppressed
   }

inline bool allowedat(const char *name16,const uint32_t t) {
   std::lock_guard<std::mutex> guard(lock);
   return allowedatlocked(name16,t);
   }

inline bool allowedat(const int sensorindex,const uint32_t t) {
   if(!sensors||sensorindex<0||sensorindex>sensors->last())
      return false;
   return allowedat(sensors->getsensor(sensorindex)->name,t);
   }

/* start of the newest epoch; timestamps below it have a final attribution that
 * no later handover can change anymore */
inline uint32_t lastepochfrom() {
   std::lock_guard<std::mutex> guard(lock);
   const int nr=epochcountlocked();
   if(nr<=0)
      return 0;
   return settings->data()->primaryepochs[nr-1].from;
   }

/* newest stream value of this sensor that belongs to the primary time series.
 * mint: stop early once values cannot beat an already found candidate. */
inline const ScanData *lastallowedstream(const SensorGlucoseData *sens,const uint32_t mint=0) {
   if(!sens)
      return nullptr;
   const char *name=sens->sensorname()->data();
   const std::span<const ScanData> polls=sens->getPolldata();
   std::lock_guard<std::mutex> guard(lock);
   for(int i=(int)polls.size()-1;i>=0;--i) {
      const ScanData *el=&polls[i];
      if(el->valid()) {
         if(el->t<=mint)
            return nullptr;
         if(allowedatlocked(name,el->t))
            return el;
         }
      }
   return nullptr;
   }

/* last stored data time of a sensor, for placing a handover boundary.
 * endtime alone can be stale (manual finish and the hasData grace path do not
 * refresh it), so always take the newer of both sources. */
inline uint32_t lastdatatimelocked(const int ind) {
   uint32_t endtime=sensors->getsensor(ind)->endtime;
   if(const SensorGlucoseData *hist=sensors->getSensorData(ind)) {
      if(const uint32_t used=hist->lastused();used>endtime)
         endtime=used;
      }
   return endtime;
   }

/* Resolves the primary index and persists an automatic handover as new epoch.
 * Returns -1 when no primary can be resolved (no alive sensor). */
inline int resolvelocked(const uint32_t nu) {
   if(!sensors||!settings||!settings->data())
      return -1;
   const char *stored=storednamelocked();
   uint32_t oldstart=0;
   uint32_t oldend=0;
   if(stored) {
      if(const int ind=sensors->sensorindex(stored);ind>=0) {
         if(alive(ind,nu))
            return ind;
         oldstart=sensors->getsensor(ind)->starttime;
         oldend=lastdatatimelocked(ind);
         }
      }
   int best=-1,fallback=-1,unstarted=-1;
   uint32_t beststart=0,fallbackstart=0;
   const int last=sensors->last();
   for(int i=0;i<=last;++i) {
      if(!alive(i,nu))
         continue;
      const uint32_t st=sensors->getsensor(i)->starttime;
      if(!st) {
         //no starttime yet (no data received): only primary when nothing else is
         if(unstarted<0)
            unstarted=i;
         continue;
         }
      if(oldstart&&st>=oldstart&&(best<0||st<beststart)) {
         best=i;
         beststart=st;
         }
      if(fallback<0||st<fallbackstart) {
         fallback=i;
         fallbackstart=st;
         }
      }
   const int take=best>=0?best:(fallback>=0?fallback:unstarted);
   if(take>=0) {
      uint32_t from;
      const uint8_t reason=stored?REASON_AUTO:REASON_MIGRATION;
      if(stored) {
         //handover: the successor takes over right after the old primary's last
         //stored value, covering the detection delay of the lifecycle check;
         //everything the successor produced before that stays suppressed
         from=oldend?oldend+1:nu;
         }
      else {
         //migration: routing starts at the chosen sensor's start, but never
         //inside data already delivered by meanwhile finished sensors
         from=sensors->getsensor(take)->starttime;
         if(!from)
            from=nu;
         for(int i=0;i<=last;++i) {
            if(i==take||!sensors->getsensor(i)->name[0])
               continue;
            sensors->checkinfo(i,nu);
            if(!sensors->getsensor(i)->finished)
               continue;
            if(const uint32_t deadend=lastdatatimelocked(i);deadend&&deadend+1>from)
               from=deadend+1;
            }
         }
      if(from>nu)
         from=nu;
      appendepochlocked(sensors->getsensor(take)->name,from,reason);
      }
   return take;
   }

inline int index() {
   std::lock_guard<std::mutex> guard(lock);
   return resolvelocked(time(nullptr));
   }

/* manual, confirmed selection from the sensor dropdown; starts a new epoch now.
 * A quick switch-back to the previous primary removes the last epoch instead of
 * appending, so accidental toggling neither burns epochs nor cuts the series. */
inline void setprimaryindex(const int ind) {
   if(!sensors||!settings||!settings->data()||ind<0||ind>sensors->last())
      return;
   std::lock_guard<std::mutex> guard(lock);
   const char *name=sensors->getsensor(ind)->name;
   const char *stored=storednamelocked();
   if(stored&&!memcmp(stored,name,sensornamelen))
      return;
   const uint32_t nu=time(nullptr);
   const int nr=epochcountlocked();
   auto *dat=settings->data();
   if(nr>=2&&!memcmp(dat->primaryepochs[nr-2].name,name,sensornamelen)
         &&nu>=dat->primaryepochs[nr-1].from
         &&(nu-dat->primaryepochs[nr-1].from)<=600) {
      dat->primaryepochnr=nr-1;
#ifdef LOGGER
      LOGGER("primarysensor undo epoch %d\n",nr-1);
#endif
      return;
      }
   appendepochlocked(name,nu,REASON_MANUAL);
   }

/* read-only snapshot of the routing state for the status endpoint; resolves a
 * pending handover first so the reported primary is current */
struct routingstatus {
   int primaryindex;
   uint32_t epochfrom;
   uint32_t routingstart;
   int epochcount;
   uint8_t reason;
   };
inline routingstatus status() {
   std::lock_guard<std::mutex> guard(lock);
   routingstatus out{resolvelocked(time(nullptr)),0,0,0,REASON_UNKNOWN};
   const int nr=epochcountlocked();
   out.epochcount=nr;
   if(nr>0) {
      const auto *dat=settings->data();
      out.epochfrom=dat->primaryepochs[nr-1].from;
      out.routingstart=dat->primaryroutingstart;
      out.reason=dat->primaryepochs[nr-1].reason;
      }
   return out;
   }
}
