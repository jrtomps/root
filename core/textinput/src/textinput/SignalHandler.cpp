//===--- SignalHandler.cpp - Signal Emission --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the interface for emitting signals.
//
//  Axel Naumann <axel@cern.ch>, 2011-05-12
//===----------------------------------------------------------------------===//

#include "textinput/SignalHandler.h"

#include <csignal>
#include <sys/signal.h> // For SIGINT when building with -fmodules

namespace textinput {
  using std::raise;

  void
  SignalHandler::EmitCtrlC() {
    raise(SIGINT);
  }

  void
  SignalHandler::EmitCtrlZ() {
#ifndef _WIN32
    raise(SIGTSTP);
#endif
  }
}
