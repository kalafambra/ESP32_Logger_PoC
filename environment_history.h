#pragma once
#include <cstdint>
#include "aggregation.h"

/* Keep fine detail for 30 s; older data uses bounded 30 s buckets across a ten-minute graph. */
class EnvironmentHistory {
 public:
  static constexpr unsigned RECENT_CAPACITY=30,OLD_CAPACITY=20,MAX_POINTS=51;
  static constexpr uint32_t FINE_MS=30000,BUCKET_MS=30000,RETENTION_MS=600000;
  struct Point {
    uint32_t firstSequence=0,sequence=0,startMs=0,endMs=0,revision=0;
    uint16_t samples=0,reports=0;
    bool ok=false;
    PublishedChannel temperature,humidity,pressure;
    /* Use the bucket centre so an average is plotted over the period it represents. */
    uint32_t midpoint()const{return startMs+(endMs-startMs)/2;}
  };

  /* Clear only volatile graph storage; sensor reporting and persistent pickup events are independent. */
  void clear(){recentHead=recentCount=oldHead=oldCount=0;pending=Point{};}

  /* Promote expired fine reports before inserting; a delayed loop never retains old fine detail. */
  void add(const PublishedSample &sample){
    while(recentCount&&(recentCount==RECENT_CAPACITY||sample.sampleMs-oldestRecent().endMs>=FINE_MS)){
      promote(oldestRecent(),sample.sequence);--recentCount;
    }
    Point p;p.firstSequence=p.sequence=sample.sequence;p.startMs=p.endMs=sample.sampleMs;
    p.revision=sample.sequence;p.samples=sample.count;p.reports=1;p.ok=sample.ok;
    p.temperature=sample.temperature;p.humidity=sample.humidity;p.pressure=sample.pressure;
    recent[recentHead]=p;recentHead=(recentHead+1)%RECENT_CAPACITY;++recentCount;
  }

  /* A revision cursor resends changed old buckets even though their source sequences are older. */
  template<typename Visitor> void visit(uint32_t since,uint32_t now,Visitor visitor)const{
    for(unsigned i=0;i<oldCount;++i)send(old[(oldHead+OLD_CAPACITY-oldCount+i)%OLD_CAPACITY],true,since,now,visitor);
    if(pending.reports)send(pending,true,since,now,visitor);
    for(unsigned i=0;i<recentCount;++i)send(recent[(recentHead+RECENT_CAPACITY-recentCount+i)%RECENT_CAPACITY],false,since,now,visitor);
  }

  /* Estimate the response allocation from selected rows rather than reserving 16 KB on every poll. */
  unsigned selected(uint32_t since,uint32_t now)const{
    unsigned count=0;visit(since,now,[&](const Point &,bool){++count;});return count;
  }

 private:
  Point recent[RECENT_CAPACITY],old[OLD_CAPACITY],pending;
  unsigned recentHead=0,recentCount=0,oldHead=0,oldCount=0;

  /* Ring position depends on retained count, allowing several old reports to expire at once. */
  const Point &oldestRecent()const{return recent[(recentHead+RECENT_CAPACITY-recentCount)%RECENT_CAPACITY];}

  /* Invalid constituent reports invalidate a whole coarse bucket; no sensor gaps are hidden. */
  static void mergeChannel(PublishedChannel &to,const PublishedChannel &from,unsigned n,unsigned added){
    if(!added)return;
    if(!n){to=from;return;}
    to.mean=(double(to.mean)*n+double(from.mean)*added)/(n+added);
    if(from.minimum<to.minimum)to.minimum=from.minimum;
    if(from.maximum>to.maximum)to.maximum=from.maximum;
  }

  /* Time-aligned buckets remain 30-second windows even when source reports are delayed or missing. */
  void promote(const Point &p,uint32_t revision){
    if(pending.reports&&(p.endMs-1)/BUCKET_MS!=(pending.endMs-1)/BUCKET_MS){
      old[oldHead]=pending;oldHead=(oldHead+1)%OLD_CAPACITY;if(oldCount<OLD_CAPACITY)++oldCount;
      pending=Point{};
    }
    if(!pending.reports)pending=p;
    else{
      mergeChannel(pending.temperature,p.temperature,pending.samples,p.samples);
      mergeChannel(pending.humidity,p.humidity,pending.samples,p.samples);
      mergeChannel(pending.pressure,p.pressure,pending.samples,p.samples);
      pending.sequence=p.sequence;pending.endMs=p.endMs;pending.samples+=p.samples;
      ++pending.reports;pending.ok=pending.ok&&p.ok;
    }
    pending.revision=revision;
  }

  /* Expire by age and send only changed records; the browser owns its already-received history. */
  template<typename Visitor> static void send(const Point &p,bool coarse,uint32_t since,uint32_t now,Visitor &visitor){
    if(p.reports&&p.revision>since&&now-p.midpoint()<=RETENTION_MS)visitor(p,coarse);
  }
};

/* Exercise aging, weighting, cursor updates, error propagation and long-run capacity before acquisition. */
inline bool runEnvironmentHistorySelfTests(EnvironmentHistory &history){
  history.clear();
  auto sample=[](unsigned i){PublishedSample p;p.sequence=i;p.sampleMs=i*1000;p.count=5;p.ok=true;
    p.temperature.mean=float(i);p.temperature.minimum=-float(i);p.temperature.maximum=float(i)+1;
    p.humidity=p.pressure=p.temperature;return p;};
  for(unsigned i=1;i<=31;++i)history.add(sample(i));
  unsigned fine=0,coarse=0;bool correct=true;
  history.visit(0,31000,[&](const EnvironmentHistory::Point &p,bool old){if(old){++coarse;correct&=p.firstSequence==1&&p.reports==1;}else ++fine;});
  if(!correct||fine!=30||coarse!=1)return false;
  for(unsigned i=32;i<=60;++i)history.add(sample(i));
  bool found=false;
  history.visit(59,60000,[&](const EnvironmentHistory::Point &p,bool old){if(old){found=true;correct&=p.reports==30&&p.samples==150&&p.temperature.mean==15.5f&&p.temperature.minimum==-30&&p.temperature.maximum==31;}});
  if(!found||!correct||history.selected(59,60000)!=2)return false;
  for(unsigned i=61;i<=1800;++i)history.add(sample(i));
  if(history.selected(0,1800000)>EnvironmentHistory::MAX_POINTS)return false;
  history.clear();auto a=sample(1);a.count=1;a.temperature.mean=10;history.add(a);
  auto b=sample(2);b.count=3;b.temperature.mean=20;history.add(b);
  history.add(sample(32));
  found=false;history.visit(0,32000,[&](const EnvironmentHistory::Point &p,bool old){if(old){found=true;correct&=p.temperature.mean==17.5f;}});
  if(!found||!correct)return false;
  history.clear();a=sample(1);a.ok=false;history.add(a);history.add(sample(31));
  history.visit(0,31000,[&](const EnvironmentHistory::Point &p,bool old){if(old)correct&=!p.ok;});
  history.clear();return correct;
}
