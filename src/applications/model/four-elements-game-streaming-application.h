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
 * Authors: Salman Mohebi <s.mohebi22@gmail.com>
 *
 */

#ifndef FOUR_ELEMENTS_GAME_STREAMING_APPLICATION_H
#define FOUR_ELEMENTS_GAME_STREAMING_APPLICATION_H

#include "ns3/mixture-random-variable.h"
#include "ns3/game-streaming-application.h"

namespace ns3 {

/**
 * \ingroup applications
 *
 * Implement the gaming traffic streams for the Four Element client based on following paper:
 * Manzano, Marc, et al. "Dissecting the protocol and network traffic of the OnLive
 * cloud gaming platform." Multimedia systems 20.5 (2014): 451-470.
 */
class FourElementsStreamingClient : public GameStreamingApplication
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief create a FourElementsStreamingClient object with default parameters
   */
  FourElementsStreamingClient ();

  virtual ~FourElementsStreamingClient () override;

private:
  /**
   *  Initialize the parameters of different streams
   */
  virtual void InitializeStreams () override;

};


/**
 * \ingroup applications
 *
 * Implement the gaming traffic streams for the 4-Elements based on following paper:
 * Manzano, Marc, et al. "Dissecting the protocol and network traffic of the OnLive
 * cloud gaming platform." Multimedia systems 20.5 (2014): 451-470.
 */
class FourElementsStreamingServer : public GameStreamingApplication
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief create a FourElementsStreamingServer object with default parameters
   */
  FourElementsStreamingServer ();

  virtual ~FourElementsStreamingServer () override;

private:
  /**
   *  Initialize the parameters of different streams
   */
  void InitializeStreams () override;

};

} // namespace ns3

#endif /* FOUR_ELEMENTS_GAME_STREAMING_APPLICATION_H */