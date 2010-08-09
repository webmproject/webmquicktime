// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


#if __MACH__
ComponentSelectorOffset(-kQTRemoveComponentPropertyListenerSelect)
#else
ComponentSelectorOffset(-kComponentVersionSelect)
#endif

ComponentRangeCount(3)
ComponentRangeShift(7)
ComponentRangeMask(7F)

ComponentRangeBegin(0)
#if __MACH__
ComponentError(RemoveComponentPropertyListener)
ComponentError(AddComponentPropertyListener)
StdComponentCall(SetComponentProperty)
StdComponentCall(GetComponentProperty)
StdComponentCall(GetComponentPropertyInfo)

ComponentError(GetPublicResource)
ComponentError(ExecuteWiredAction)
ComponentError(GetMPWorkFunction)
ComponentError(Unregister)

ComponentError(Target)
ComponentError(Register)
#else
#error __MACH__ not defined
#endif
StdComponentCall(Version)
StdComponentCall(CanDo)
StdComponentCall(Close)
StdComponentCall(Open)
ComponentRangeEnd(0)

ComponentRangeUnused(1)

ComponentRangeBegin(2)
ComponentError(ToHandle)
ComponentCall(ToFile)
ComponentError(130)
ComponentError(GetAuxiliaryData)
ComponentCall(SetProgressProc)
ComponentError(SetSampleDescription)
ComponentCall(DoUserDialog)
ComponentError(GetCreatorType)
ComponentCall(ToDataRef)
ComponentCall(FromProceduresToDataRef)
ComponentCall(AddDataSource)
ComponentCall(Validate)
ComponentCall(GetSettingsAsAtomContainer)
ComponentCall(SetSettingsFromAtomContainer)
ComponentCall(GetFileNameExtension)
ComponentCall(GetShortFileTypeString)
ComponentCall(GetSourceMediaType)
ComponentError(SetGetMoviePropertyProc)
ComponentRangeEnd(2)

ComponentRangeBegin(3)
ComponentCall(NewGetDataAndPropertiesProcs)
ComponentCall(DisposeGetDataAndPropertiesProcs)
ComponentRangeEnd(3)
