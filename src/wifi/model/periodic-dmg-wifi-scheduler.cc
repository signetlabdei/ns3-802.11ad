/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2020, University of Padova, Department of Information
 * Engineering, SIGNET Lab.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 */

#include <algorithm>    // std::sort
#include <ns3/assert.h>
#include <ns3/log.h>
#include <ns3/simulator.h>
#include <ns3/pointer.h>
#include <ns3/boolean.h>

#include "periodic-dmg-wifi-scheduler.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("PeriodicDmgWifiScheduler");

NS_OBJECT_ENSURE_REGISTERED (PeriodicDmgWifiScheduler);

TypeId
PeriodicDmgWifiScheduler::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PeriodicDmgWifiScheduler")
      .SetParent<DmgWifiScheduler> ()
      .SetGroupName ("Wifi")
      .AddConstructor<PeriodicDmgWifiScheduler> ()

      .AddAttribute ("MinBroadcastCbapDuration", "The minimum duration in microseconds of a broadcast CBAP in the DTI",
                     UintegerValue (4096),
                     MakeUintegerAccessor (&PeriodicDmgWifiScheduler::m_minBroadcastCbapDuration),
                     MakeUintegerChecker<uint32_t> ())
      .AddAttribute ("InterAllocationDistance", "The time distance in microseconds between two adjacent allocations "
                     "This distance will be allocated as broadcast CBAP",
                     UintegerValue (10),
                     MakeUintegerAccessor (&PeriodicDmgWifiScheduler::m_interAllocationDistance),
                     MakeUintegerChecker<uint32_t> (10, 65535))
  ;
  return tid;
}

PeriodicDmgWifiScheduler::PeriodicDmgWifiScheduler ()
{
  NS_LOG_FUNCTION (this);
}

PeriodicDmgWifiScheduler::~PeriodicDmgWifiScheduler ()
{
  NS_LOG_FUNCTION (this);
}

void
PeriodicDmgWifiScheduler::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  DmgWifiScheduler::DoDispose ();
}

void
PeriodicDmgWifiScheduler::UpdateStartAndRemainingTime (void)
{
  // for the periodic scheduler, m_allocationStartTime is useless, since the addition
  // of new SPs is consecutive 
  NS_LOG_FUNCTION (this);
  if (m_addtsAllocationList.empty ())
    {
      // no existing allocations
      m_remainingDtiTime = m_dtiDuration.GetMicroSeconds ();
      m_availableSlots.push_back (std::make_pair (0, m_dtiDuration.GetMicroSeconds ()));
    }
  else
    {
      // if there are existing allocations, update DTI time just for consistency
      m_remainingDtiTime = 0;
      for (const auto & slot: m_availableSlots)
      {
        m_remainingDtiTime += (slot.second - slot.first);
      }
    }
}

void
PeriodicDmgWifiScheduler::AdjustExistingAllocations (AllocationFieldListI iter, uint32_t duration, bool isToAdd)
{
  NS_LOG_FUNCTION (this << duration << isToAdd);
  
  // This method is called upon a DelTsRequest or after the cleanup of 
  // non-pseudostatic allocations.
  // In this version of the periodic scheduler, existing allocations are not shifted 
  // to fill the created gaps but only the vector listing the available slots is updated.
  // For this reason, the current input parameters are useless.
   
  auto addtsListCopy = m_addtsAllocationList;
  
  // sort the copy to simplify the process of going through the allocation list
  sort (addtsListCopy.begin (),
        addtsListCopy.end (),
        [](const AllocationField& lhs, const AllocationField& rhs){
      return lhs.GetAllocationStart () < rhs.GetAllocationStart ();
    });
  
  uint32_t startSlot = 0;
  std::vector<std::pair<uint32_t, uint32_t> > newDTI; 
  
  for (const auto & allocation: addtsListCopy)
  {
    // this loop goes through the list of allocations, spotting all the available
    // slots and adding them to the list
    if (startSlot < allocation.GetAllocationStart())
    {
      newDTI.push_back (std::make_pair (startSlot, allocation.GetAllocationStart()));
    }
    
    startSlot = allocation.GetAllocationStart() + allocation.GetAllocationBlockDuration() + m_guardTime;
  }
  
  // the following check ensure that if there is a trailing available slot in the DTI,
  // it will be correctly considered and added to the list.
  if (startSlot < m_dtiDuration.GetMicroSeconds ())
  {
    newDTI.push_back (std::make_pair (startSlot,  m_dtiDuration.GetMicroSeconds ()));
  }
  
  m_availableSlots = newDTI;
  
}

uint32_t
PeriodicDmgWifiScheduler::GetAllocationDuration (uint32_t minAllocation, uint32_t maxAllocation)
{
  NS_LOG_FUNCTION (this << minAllocation << maxAllocation);
  return ((minAllocation + maxAllocation) / 2);
}

StatusCode
PeriodicDmgWifiScheduler::AddNewAllocation (uint8_t sourceAid, const DmgTspecElement &dmgTspec, const DmgAllocationInfo &info)
{
  NS_LOG_FUNCTION (this);

  StatusCode status;
  uint32_t allocDuration;
  if (info.GetAllocationFormat () == ISOCHRONOUS)
    {
      allocDuration = GetAllocationDuration (dmgTspec.GetMinimumAllocation (), dmgTspec.GetMaximumAllocation ());
    }
  else if (info.GetAllocationFormat () == ASYNCHRONOUS)
    {
      // for asynchronous allocations, the Maximum Allocation field is reserved (IEEE 802.11ad 8.4.2.136)
      allocDuration = dmgTspec.GetMinimumAllocation ();
    }
  else
    {
      NS_FATAL_ERROR ("Allocation Format not supported");
    }

  // Implementation of an admission policy for newly received requests. 
  uint16_t allocPeriod = dmgTspec.GetAllocationPeriod ();
  if (allocPeriod != 0)
    {
      // Proceed with the allocation of periodic SPs 
      bool isMultiple = dmgTspec.IsAllocationPeriodMultipleBI ();
      if (isMultiple)
        {
          NS_FATAL_ERROR ("Multiple BI periodicity is not supported yet.");
        }
      
      // spInterval is going to be passed to AddAllocationPeriod to specify the 
      // distance between consecutive periodic SPs
      uint32_t spInterval = uint32_t (m_biDuration.GetMicroSeconds () / allocPeriod);
      if (spInterval - allocDuration < m_minBroadcastCbapDuration)
        {
          NS_FATAL_ERROR ("These settings cannot guarantee minimum CBAP duration.");
        }

      NS_LOG_DEBUG ("Allocation Period " << uint32_t (allocPeriod) << " AllocDuration " << allocDuration << " Multiple " << isMultiple);
      NS_LOG_DEBUG ("Schedule one SP every " << spInterval);

      uint32_t startPeriodicAllocation = m_availableSlots[0].first;
      uint32_t startFirstAllocation = startPeriodicAllocation;
      uint32_t endAlloc;
      uint8_t counter = 0;

      uint8_t blocks = GetAvailableBlocks (allocDuration, spInterval);

      if (blocks > 1)
        {
          while (counter < blocks)
            {
              NS_LOG_DEBUG ("Reserve from " << startPeriodicAllocation << " for " << allocDuration);

              endAlloc = startPeriodicAllocation + allocDuration + m_guardTime;
              UpdateAvailableSlots (startPeriodicAllocation, endAlloc);

              startPeriodicAllocation += spInterval;
              counter++;

              // update remaining DTI time for consistency
              m_remainingDtiTime -= (allocDuration + m_guardTime);
            }

          AddAllocationPeriod (info.GetAllocationID (), info.GetAllocationType (), info.IsPseudoStatic (),
                               sourceAid, info.GetDestinationAid (),
                               startFirstAllocation, allocDuration, spInterval, blocks);
          status.SetSuccess ();
        }
      else
        {
          // if we cannot guarantee AT LEAST TWO periodic SPs, the request is rejected.
          status.SetFailure ();
        }

    }
  else
    {

      if (m_availableSlots.begin () == m_availableSlots.end ())
        {
          NS_LOG_DEBUG ("There are no free available slots in the DTI.");
          status.SetFailure ();
        }

      uint32_t endAlloc;
      uint32_t slotDur;
      for (auto it = m_availableSlots.begin (); it != m_availableSlots.end (); ++it)
        {
          slotDur = it->second - it->first;

          if (slotDur > allocDuration)
            {

              endAlloc = AllocateSingleContiguousBlock (info.GetAllocationID (), info.GetAllocationType (), info.IsPseudoStatic (),
                                                        sourceAid, info.GetDestinationAid (), it->first, allocDuration);
              m_remainingDtiTime -= (allocDuration + m_guardTime);
              
              // The following function modifies m_availableSlots, on which this
              // loop is iterating. Commonly, it is a bad practice but, as we break 
              // the loop if we enter this condition, the damage is avoided.
              UpdateAvailableSlots (it->first, endAlloc);
              status.SetSuccess ();
              break;
            }
          else
            {
              status.SetFailure ();
            }
        }

    }

  return status;
}

uint8_t
PeriodicDmgWifiScheduler::GetAvailableBlocks (uint32_t allocDuration, uint32_t spInterval)
{
  NS_LOG_FUNCTION (this);

  auto it = m_availableSlots.begin ();
  uint32_t startAlloc = it->first;
  uint32_t slotDuration = 0;
  uint8_t blocks = 0;

  while (it != m_availableSlots.end ())
    {
      if (startAlloc < it->first)
        {
          // Periodicity has been broken, we stop the algorithm.
          break;
        }

      slotDuration = it->second - startAlloc;

      if (allocDuration + m_guardTime > slotDuration)
        {
          if (blocks == 0)
            {
              // go on until eventually find the first available slot that fits this SP
              // note that this also cover the condition where no slot satisfies the requirement
              ++it;
              continue;
            }
          else
            {
              // if one or more periodic SPs already allocated, now the periodicity
              // is broken and the algorithm stops
              break;
            }
        }

      blocks++;
      startAlloc += spInterval;

      // immediately check the next periodic allocation, with respect to the
      // current available slot that we are using

      if (startAlloc == it->second)
        {
          // if the next SP should start at the end of the current slot, 
          // it cannot be added and the algorithm stops
          break;
        }
      else if (startAlloc < it->second)
        {
          // if the the next SP should start in the current slot, immediately check if there is
          // enough free time to place it, otherwise the algorithm stops
          if (startAlloc + allocDuration + m_guardTime > it->second)
            {
              break;
            }
        }
      else
        {
          // pass to the next slot only if startAlloc goes beyond the end of the current one
          ++it;
        }
    }

  return blocks;
}

void
PeriodicDmgWifiScheduler::UpdateAvailableSlots (uint32_t startAllocation, uint32_t endAlloc, uint32_t difference)
{
  NS_LOG_FUNCTION (this);

  std::vector<std::pair<uint32_t, uint32_t> > newDTI;
  
  if (difference > 0)
    {
      // something has changed in the allocation list, need to change the list of 
      // available slots accordingly
      bool search = true;
      for (const auto & slot: m_availableSlots)
        {

          if (slot.first < startAllocation)
            {
              newDTI.push_back (slot);
              continue;
            }

          if (endAlloc < slot.first)
            {
              // the following snippet ensure that the search is carried on until 
              // we find the empty gap created by the allocation time reduction
              // and we add it to the list of available slots 
              if (search)
                {
                  if (difference == (slot.first - endAlloc))
                    {
                      // this make sure that two adjacent available slots are merged 
                      newDTI.push_back (std::make_pair (endAlloc, slot.second));
                      search = false;
                    }
                  else if (difference < (slot.first - endAlloc))
                    {
                      // this condition is verified when there is one or more allocations 
                      // between the gap and the following available slot
                      newDTI.push_back (std::make_pair (endAlloc, endAlloc + difference));
                      newDTI.push_back (slot);
                      search = false;
                    }
                  NS_ASSERT_MSG(difference <= (slot.first - endAlloc), "Something broke in runtime, check the update of the available slots.");
                }
              else
                {
                  newDTI.push_back (slot);
                }
            }
        }
    }
  else
    {
      for (const auto & slot: m_availableSlots)
        {
          if (slot.first > endAlloc || slot.second < startAllocation)
            {
              newDTI.push_back (slot);
              continue;
            }
          
          if (slot.first == startAllocation)
            {
              newDTI.push_back (std::make_pair (endAlloc, slot.second));
            }
          else if (slot.first < startAllocation && slot.second > endAlloc)
            {
              newDTI.push_back (std::make_pair (slot.first, startAllocation));
              newDTI.push_back (std::make_pair (endAlloc, slot.second));
            }
        }
    }

  m_availableSlots = newDTI;

  for (const auto & slot : m_availableSlots)
    {
      NS_LOG_DEBUG ("Available slot from " << slot.first << " to " << slot.second);
    }

}

StatusCode
PeriodicDmgWifiScheduler::ModifyExistingAllocation (uint8_t sourceAid, const DmgTspecElement &dmgTspec, const DmgAllocationInfo &info)
{
  NS_LOG_FUNCTION (this);

  StatusCode status;
  uint32_t newDuration;
  if (info.GetAllocationFormat () == ISOCHRONOUS)
    {
      newDuration = GetAllocationDuration (dmgTspec.GetMinimumAllocation (), dmgTspec.GetMaximumAllocation ());
    }
  else if (info.GetAllocationFormat () == ASYNCHRONOUS)
    {
      // for asynchronous allocations, the Maximum Allocation field is reserved (IEEE 802.11ad 8.4.2.136) 
      newDuration = dmgTspec.GetMinimumAllocation ();
    }
  else
    {
      NS_FATAL_ERROR ("Allocation Format not supported");
    }

  AllocationFieldListI allocation;
  // Retrieve the allocation for which a modification has been requested
  for (allocation = m_addtsAllocationList.begin (); allocation != m_addtsAllocationList.end ();)
    {
      if ((allocation->GetAllocationID () == info.GetAllocationID ())
          && (allocation->GetSourceAid () == sourceAid) && (allocation->GetDestinationAid () == info.GetDestinationAid ()))
        {
          break;
        }
      else
        {
          ++allocation;
        }
    }

  if (allocation == m_addtsAllocationList.end ())
    {
      NS_FATAL_ERROR ("Required allocation does not exist.");
    }

  uint32_t currentDuration = allocation->GetAllocationBlockDuration ();
  NS_LOG_DEBUG ("current duration=" << currentDuration << ", new duration=" << newDuration);
  uint32_t timeDifference;
  if (newDuration > currentDuration)
    {
      NS_LOG_DEBUG ("This feature is not supported.");
      // The request cannot be accepted; maintaining old allocation duration
      // No need to update allocation start time and remaining DTI time 
      status.SetFailure ();
    }
  else
    {
      NS_LOG_DEBUG ("Reduction of the duration is always allowed. Proceed to update the available slots...");
      timeDifference = currentDuration - newDuration;
      allocation->SetAllocationBlockDuration (newDuration);
      status.SetSuccess ();
      
      uint32_t startAlloc = allocation->GetAllocationStart ();
      uint32_t endAlloc = allocation->GetAllocationStart () + allocation->GetAllocationBlockDuration () + m_guardTime;
      
      uint16_t allocPeriod = allocation->GetAllocationBlockPeriod ();
      if (allocPeriod != 0)
        {
          // If the allocation to modify is periodic, we retrieve the number of blocks
          // and update the available slots in the DTI accordingly.
          // TODO: update also the number of blocks if the new duration allows to 
          // add further blocks
          uint8_t blocks = allocation->GetNumberOfBlocks();
          for (uint8_t i = 0; i < blocks; i++)
            {
              NS_LOG_DEBUG ("Modify SP Block [" << +i << "] at " << startAlloc << " till " << endAlloc);
              UpdateAvailableSlots (startAlloc, endAlloc, timeDifference);
              startAlloc += allocPeriod;
              endAlloc += allocPeriod;
            }
        }
      else
        {
          UpdateAvailableSlots (startAlloc, endAlloc, timeDifference);          
        }

    }

  return status;
}

void
PeriodicDmgWifiScheduler::AddBroadcastCbapAllocations (void)
{
  NS_LOG_FUNCTION (this);
  /* Addts allocation list is copied to the allocation list */

  m_allocationList = m_addtsAllocationList;
  AllocationFieldList broadcastCbapList;

  // fill all the remaining available slots with broadcast CBAPs

  for (const auto & slot : m_availableSlots)
    {
      broadcastCbapList = GetBroadcastCbapAllocation (true, slot.first, slot.second - slot.first);
      m_remainingDtiTime -= slot.second - slot.first;
      m_allocationList.insert (m_allocationList.begin (), broadcastCbapList.begin (), broadcastCbapList.end ());
      NS_LOG_DEBUG ("Added broadcast CBAPs list of size: " << broadcastCbapList.size () << " for a total duration of " << slot.second - slot.first);
    }

  sort (m_allocationList.begin (),
        m_allocationList.end (),
        [](const AllocationField& lhs, const AllocationField& rhs){
      return lhs.GetAllocationStart () < rhs.GetAllocationStart ();
    });

  for (const auto & alloc: m_allocationList)
    {
      NS_LOG_DEBUG ("Allocation element start at: " << alloc.GetAllocationStart () << " periodicity " << alloc.GetAllocationBlockPeriod () << " duration " << alloc.GetAllocationBlockDuration ());
    }

}

} // namespace ns3