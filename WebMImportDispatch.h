// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

ComponentSelectorOffset(6)

ComponentRangeCount(1)
ComponentRangeShift(7)
ComponentRangeMask(7F)

ComponentRangeBegin(0)
  ComponentError(Target)
  ComponentError(Register)
  StdComponentCall(Version)
  StdComponentCall(CanDo)
  StdComponentCall(Close)
  StdComponentCall(Open)
ComponentRangeEnd(0)

ComponentRangeBegin(1)
  ComponentError (0)
  ComponentError (Handle)
  ComponentCall (File)
  ComponentError (SetSampleDuration)
  ComponentError (SetSampleDescription)
  ComponentError (SetMediaFile)
  ComponentError (SetDimensions)
  ComponentError (SetChunkSize)
  ComponentError (SetProgressProc)
  ComponentError (SetAuxiliaryData)
  ComponentError (SetFromScrap)
  ComponentError (DoUserDialog)
  ComponentError (SetDuration)
  ComponentError (GetAuxiliaryDataType)
  ComponentError (Validate)
  ComponentError (GetFileType)
  ComponentCall (DataRef)
  ComponentError (GetSampleDescription)
  ComponentCall (GetMIMETypeList)
  ComponentError (SetOffsetAndLimit)
  ComponentError (GetSettingsAsAtomContainer)
  ComponentError (SetSettingsFromAtomContainer)
  ComponentError (SetOffsetAndLimit64)
  ComponentError (Idle)
  ComponentError (ValidateDataRef)
  ComponentError (GetLoadState)
  ComponentError (GetMaxLoadedTime)
  ComponentError (EstimateCompletionTime)
  ComponentError (SetDontBlock)
  ComponentError (GetDontBlock)
  ComponentError (SetIdleManager)
  ComponentError (SetNewMovieFlags)
ComponentRangeEnd(1)
