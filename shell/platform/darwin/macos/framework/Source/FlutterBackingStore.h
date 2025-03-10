// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>

/**
 * Interface for backing store handles. Typically contain references to the buffers that
 * are handed by the `FlutterView` to the `FlutterRenderer`.
 */
@interface FlutterRenderBackingStore : NSObject

@end

/**
 * Wraps a Metal texture.
 */
@interface FlutterMetalRenderBackingStore : FlutterRenderBackingStore

/**
 * MTLTexture referenced by this backing store instance.
 */
@property(nonnull, nonatomic, readonly) id<MTLTexture> texture;

/**
 * Initializes a backing store with the specified MTLTexture.
 */
- (nonnull instancetype)initWithTexture:(nonnull id<MTLTexture>)texture;

@end
