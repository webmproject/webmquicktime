// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.


/*kImageCodecGetDITLForSizeSelect    = 0x002E
kImageCodecDITLInstallSelect       = 0x002F
kImageCodecDITLEventSelect         = 0x0030
kImageCodecDITLItemSelect          = 0x0031
kImageCodecDITLRemoveSelect        = 0x0032
kImageCodecDITLValidateInputSelect = 0x0033*/


ComponentSelectorOffset(8)

ComponentRangeCount(1)
ComponentRangeShift(8)
ComponentRangeMask(FF)

ComponentRangeBegin(0)
ComponentError(GetMPWorkFunction)
ComponentError(Unregister)
StdComponentCall(Target)
ComponentError(Register)
StdComponentCall(Version)
StdComponentCall(CanDo)
StdComponentCall(Close)
StdComponentCall(Open)
ComponentRangeEnd(0)

ComponentRangeBegin(1)
ComponentCall(GetCodecInfo)                         // 0
ComponentError(GetCompressionTime)
ComponentCall(GetMaxCompressionSize)
ComponentError(PreCompress)
ComponentError(BandCompress)
ComponentError(PreDecompress)                           // 5
ComponentError(BandDecompress)
ComponentError(Busy)
ComponentError(GetCompressedImageSize)
ComponentError(GetSimilarity)
ComponentError(TrimImage)                               // 10
ComponentCall(RequestSettings)
ComponentCall(GetSettings)
ComponentCall(SetSettings)
ComponentError(Flush)
ComponentError(SetTimeCode)                         // 15
ComponentError(IsImageDescriptionEquivalent)
ComponentError(NewMemory)
ComponentError(DisposeMemory)
ComponentError(HitTestData)
ComponentError(NewImageBufferMemory)                    // 20
ComponentError(ExtractAndCombineFields)
ComponentError(GetMaxCompressionSizeWithSources)
ComponentError(SetTimeBase)
ComponentError(SourceChanged)
ComponentError(FlushLastFrame)                          // 25
ComponentError(GetSettingsAsText)
ComponentError(GetParameterListHandle)
ComponentError(GetParameterList)
ComponentError(CreateStandardParameterDialog)
ComponentError(IsStandardParameterDialogEvent)          // 30
ComponentError(DismissStandardParameterDialog)
ComponentError(StandardParameterDialogDoAction)
ComponentError(NewImageGWorld)
ComponentError(DisposeImageGWorld)
ComponentError(HitTestDataWithFlags)                    // 35
ComponentError(ValidateParameters)
ComponentError(GetBaseMPWorkFunction)
ComponentError(LockBits)
ComponentError(UnlockBits)
ComponentError(RequestGammaLevel)                       // 40
ComponentError(GetSourceDataGammaLevel)
ComponentError(42)
ComponentError(GetDecompressLatency)
ComponentError(MergeFloatingImageOntoWindow)
ComponentError(RemoveFloatingImage)                 // 45
ComponentCall(GetDITLForSize)
ComponentCall(DITLInstall)
ComponentCall(DITLEvent)
ComponentCall(DITLItem)
ComponentCall(DITLRemove)                              // 50
ComponentCall(DITLValidateInput)
ComponentError(52)
ComponentError(53)
ComponentError(GetPreferredChunkSizeAndAlignment)
ComponentCall(PrepareToCompressFrames)                  // 55
ComponentCall(EncodeFrame)
ComponentCall(CompleteFrame)
ComponentCall(BeginPass)
ComponentCall(EndPass)
ComponentCall(ProcessBetweenPasses)                    // 60
ComponentRangeEnd(1)
