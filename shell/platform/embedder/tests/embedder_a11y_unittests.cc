// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Allow access to fml::MessageLoop::GetCurrent() in order to flush platform
// thread tasks.
#define FML_USED_ON_EMBEDDER

#include <functional>

#include "flutter/fml/macros.h"
#include "flutter/fml/message_loop.h"
#include "flutter/fml/synchronization/waitable_event.h"
#include "flutter/lib/ui/semantics/semantics_node.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/embedder/tests/embedder_config_builder.h"
#include "flutter/testing/testing.h"

// CREATE_NATIVE_ENTRY is leaky by design
// NOLINTBEGIN(clang-analyzer-core.StackAddressEscape)

namespace flutter {
namespace testing {

using EmbedderA11yTest = testing::EmbedderTest;

TEST_F(EmbedderTest, CannotProvideNewAndLegacySemanticsCallback) {
  EmbedderConfigBuilder builder(
      GetEmbedderContext(EmbedderTestContextType::kSoftwareContext));
  builder.SetSoftwareRendererConfig();
  builder.GetProjectArgs().update_semantics_callback =
      [](const FlutterSemanticsUpdate* update, void* user_data) {};
  builder.GetProjectArgs().update_semantics_node_callback =
      [](const FlutterSemanticsNode* update, void* user_data) {};
  builder.GetProjectArgs().update_semantics_custom_action_callback =
      [](const FlutterSemanticsCustomAction* update, void* user_data) {};
  auto engine = builder.InitializeEngine();
  ASSERT_FALSE(engine.is_valid());
  engine.reset();
}

TEST_F(EmbedderA11yTest, A11yTreeIsConsistent) {
#if defined(OS_FUCHSIA)
  GTEST_SKIP() << "This test crashes on Fuchsia. https://fxbug.dev/87493 ";
#endif  // OS_FUCHSIA

  auto& context = GetEmbedderContext(EmbedderTestContextType::kOpenGLContext);

  fml::AutoResetWaitableEvent signal_native_latch;

  // Called by the Dart text fixture on the UI thread to signal that the C++
  // unittest should resume.
  context.AddNativeCallback(
      "SignalNativeTest",
      CREATE_NATIVE_ENTRY(([&signal_native_latch](Dart_NativeArguments) {
        signal_native_latch.Signal();
      })));

  // Called by test fixture on UI thread to pass data back to this test.
  NativeEntry notify_semantics_enabled_callback;
  context.AddNativeCallback(
      "NotifySemanticsEnabled",
      CREATE_NATIVE_ENTRY(
          ([&notify_semantics_enabled_callback](Dart_NativeArguments args) {
            ASSERT_NE(notify_semantics_enabled_callback, nullptr);
            notify_semantics_enabled_callback(args);
          })));

  NativeEntry notify_accessibility_features_callback;
  context.AddNativeCallback(
      "NotifyAccessibilityFeatures",
      CREATE_NATIVE_ENTRY((
          [&notify_accessibility_features_callback](Dart_NativeArguments args) {
            ASSERT_NE(notify_accessibility_features_callback, nullptr);
            notify_accessibility_features_callback(args);
          })));

  NativeEntry notify_semantics_action_callback;
  context.AddNativeCallback(
      "NotifySemanticsAction",
      CREATE_NATIVE_ENTRY(
          ([&notify_semantics_action_callback](Dart_NativeArguments args) {
            ASSERT_NE(notify_semantics_action_callback, nullptr);
            notify_semantics_action_callback(args);
          })));

  fml::AutoResetWaitableEvent semantics_update_latch;
  context.SetSemanticsUpdateCallback([&](const FlutterSemanticsUpdate* update) {
    ASSERT_EQ(size_t(4), update->nodes_count);
    ASSERT_EQ(size_t(1), update->custom_actions_count);

    for (size_t i = 0; i < update->nodes_count; i++) {
      const FlutterSemanticsNode* node = update->nodes + i;

      ASSERT_EQ(1.0, node->transform.scaleX);
      ASSERT_EQ(2.0, node->transform.skewX);
      ASSERT_EQ(3.0, node->transform.transX);
      ASSERT_EQ(4.0, node->transform.skewY);
      ASSERT_EQ(5.0, node->transform.scaleY);
      ASSERT_EQ(6.0, node->transform.transY);
      ASSERT_EQ(7.0, node->transform.pers0);
      ASSERT_EQ(8.0, node->transform.pers1);
      ASSERT_EQ(9.0, node->transform.pers2);

      if (node->id == 128) {
        ASSERT_EQ(0x3f3, node->platform_view_id);
      } else {
        ASSERT_NE(kFlutterSemanticsNodeIdBatchEnd, node->id);
        ASSERT_EQ(0, node->platform_view_id);
      }
    }

    semantics_update_latch.Signal();
  });

  EmbedderConfigBuilder builder(context);
  builder.SetSoftwareRendererConfig();
  builder.SetDartEntrypoint("a11y_main");

  auto engine = builder.LaunchEngine();
  ASSERT_TRUE(engine.is_valid());

  // Wait for initial NotifySemanticsEnabled(false).
  fml::AutoResetWaitableEvent notify_semantics_enabled_latch;
  notify_semantics_enabled_callback = [&](Dart_NativeArguments args) {
    bool enabled = true;
    auto handle = Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_FALSE(Dart_IsError(handle));
    ASSERT_FALSE(enabled);
    notify_semantics_enabled_latch.Signal();
  };
  notify_semantics_enabled_latch.Wait();

  // Prepare to NotifyAccessibilityFeatures call
  fml::AutoResetWaitableEvent notify_features_latch;
  notify_accessibility_features_callback = [&](Dart_NativeArguments args) {
    bool enabled = true;
    auto handle = Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_FALSE(Dart_IsError(handle));
    ASSERT_FALSE(enabled);
    notify_features_latch.Signal();
  };

  // Enable semantics. Wait for NotifySemanticsEnabled(true).
  fml::AutoResetWaitableEvent notify_semantics_enabled_latch_2;
  notify_semantics_enabled_callback = [&](Dart_NativeArguments args) {
    bool enabled = false;
    auto handle = Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_FALSE(Dart_IsError(handle));
    ASSERT_TRUE(enabled);
    notify_semantics_enabled_latch_2.Signal();
  };
  auto result = FlutterEngineUpdateSemanticsEnabled(engine.get(), true);
  ASSERT_EQ(result, FlutterEngineResult::kSuccess);
  notify_semantics_enabled_latch_2.Wait();

  // Wait for initial accessibility features (reduce_motion == false)
  notify_features_latch.Wait();

  // Set accessibility features: (reduce_motion == true)
  fml::AutoResetWaitableEvent notify_features_latch_2;
  notify_accessibility_features_callback = [&](Dart_NativeArguments args) {
    bool enabled = false;
    auto handle = Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_FALSE(Dart_IsError(handle));
    ASSERT_TRUE(enabled);
    notify_features_latch_2.Signal();
  };
  result = FlutterEngineUpdateAccessibilityFeatures(
      engine.get(), kFlutterAccessibilityFeatureReduceMotion);
  ASSERT_EQ(result, FlutterEngineResult::kSuccess);
  notify_features_latch_2.Wait();

  // Wait for UpdateSemantics callback on platform (current) thread.
  signal_native_latch.Wait();
  fml::MessageLoop::GetCurrent().RunExpiredTasksNow();
  semantics_update_latch.Wait();

  // Dispatch a tap to semantics node 42. Wait for NotifySemanticsAction.
  fml::AutoResetWaitableEvent notify_semantics_action_latch;
  notify_semantics_action_callback = [&](Dart_NativeArguments args) {
    int64_t node_id = 0;
    Dart_GetNativeIntegerArgument(args, 0, &node_id);
    ASSERT_EQ(42, node_id);

    int64_t action_id;
    auto handle = Dart_GetNativeIntegerArgument(args, 1, &action_id);
    ASSERT_FALSE(Dart_IsError(handle));
    ASSERT_EQ(static_cast<int32_t>(flutter::SemanticsAction::kTap), action_id);

    Dart_Handle semantic_args = Dart_GetNativeArgument(args, 2);
    int64_t data;
    Dart_Handle dart_int = Dart_ListGetAt(semantic_args, 0);
    Dart_IntegerToInt64(dart_int, &data);
    ASSERT_EQ(2, data);

    dart_int = Dart_ListGetAt(semantic_args, 1);
    Dart_IntegerToInt64(dart_int, &data);
    ASSERT_EQ(1, data);
    notify_semantics_action_latch.Signal();
  };
  std::vector<uint8_t> bytes({2, 1});
  result = FlutterEngineDispatchSemanticsAction(
      engine.get(), 42, kFlutterSemanticsActionTap, &bytes[0], bytes.size());
  ASSERT_EQ(result, FlutterEngineResult::kSuccess);
  notify_semantics_action_latch.Wait();

  // Disable semantics. Wait for NotifySemanticsEnabled(false).
  fml::AutoResetWaitableEvent notify_semantics_enabled_latch_3;
  notify_semantics_enabled_callback = [&](Dart_NativeArguments args) {
    bool enabled = true;
    Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_FALSE(enabled);
    notify_semantics_enabled_latch_3.Signal();
  };
  result = FlutterEngineUpdateSemanticsEnabled(engine.get(), false);
  ASSERT_EQ(result, FlutterEngineResult::kSuccess);
  notify_semantics_enabled_latch_3.Wait();
}

TEST_F(EmbedderA11yTest, A11yTreeIsConsistentUsingLegacyCallbacks) {
  auto& context = GetEmbedderContext(EmbedderTestContextType::kOpenGLContext);

  fml::AutoResetWaitableEvent signal_native_latch;

  // Called by the Dart text fixture on the UI thread to signal that the C++
  // unittest should resume.
  context.AddNativeCallback(
      "SignalNativeTest",
      CREATE_NATIVE_ENTRY(([&signal_native_latch](Dart_NativeArguments) {
        signal_native_latch.Signal();
      })));

  // Called by test fixture on UI thread to pass data back to this test.
  NativeEntry notify_semantics_enabled_callback;
  context.AddNativeCallback(
      "NotifySemanticsEnabled",
      CREATE_NATIVE_ENTRY(
          ([&notify_semantics_enabled_callback](Dart_NativeArguments args) {
            ASSERT_NE(notify_semantics_enabled_callback, nullptr);
            notify_semantics_enabled_callback(args);
          })));

  NativeEntry notify_accessibility_features_callback;
  context.AddNativeCallback(
      "NotifyAccessibilityFeatures",
      CREATE_NATIVE_ENTRY((
          [&notify_accessibility_features_callback](Dart_NativeArguments args) {
            ASSERT_NE(notify_accessibility_features_callback, nullptr);
            notify_accessibility_features_callback(args);
          })));

  NativeEntry notify_semantics_action_callback;
  context.AddNativeCallback(
      "NotifySemanticsAction",
      CREATE_NATIVE_ENTRY(
          ([&notify_semantics_action_callback](Dart_NativeArguments args) {
            ASSERT_NE(notify_semantics_action_callback, nullptr);
            notify_semantics_action_callback(args);
          })));

  fml::AutoResetWaitableEvent semantics_node_latch;
  fml::AutoResetWaitableEvent semantics_action_latch;

  int node_batch_end_count = 0;
  int action_batch_end_count = 0;

  int node_count = 0;
  context.SetSemanticsNodeCallback([&](const FlutterSemanticsNode* node) {
    if (node->id == kFlutterSemanticsNodeIdBatchEnd) {
      ++node_batch_end_count;
      semantics_node_latch.Signal();
    } else {
      // Batches should be completed after all nodes are received.
      ASSERT_EQ(0, node_batch_end_count);
      ASSERT_EQ(0, action_batch_end_count);

      ++node_count;
      ASSERT_EQ(1.0, node->transform.scaleX);
      ASSERT_EQ(2.0, node->transform.skewX);
      ASSERT_EQ(3.0, node->transform.transX);
      ASSERT_EQ(4.0, node->transform.skewY);
      ASSERT_EQ(5.0, node->transform.scaleY);
      ASSERT_EQ(6.0, node->transform.transY);
      ASSERT_EQ(7.0, node->transform.pers0);
      ASSERT_EQ(8.0, node->transform.pers1);
      ASSERT_EQ(9.0, node->transform.pers2);

      if (node->id == 128) {
        ASSERT_EQ(0x3f3, node->platform_view_id);
      } else {
        ASSERT_EQ(0, node->platform_view_id);
      }
    }
  });

  int action_count = 0;
  context.SetSemanticsCustomActionCallback(
      [&](const FlutterSemanticsCustomAction* action) {
        if (action->id == kFlutterSemanticsCustomActionIdBatchEnd) {
          ++action_batch_end_count;
          semantics_action_latch.Signal();
        } else {
          // Batches should be completed after all actions are received.
          ASSERT_EQ(0, node_batch_end_count);
          ASSERT_EQ(0, action_batch_end_count);

          ++action_count;
        }
      });

  EmbedderConfigBuilder builder(context);
  builder.SetSoftwareRendererConfig();
  builder.SetDartEntrypoint("a11y_main");

  auto engine = builder.LaunchEngine();
  ASSERT_TRUE(engine.is_valid());

  // Wait for initial NotifySemanticsEnabled(false).
  fml::AutoResetWaitableEvent notify_semantics_enabled_latch;
  notify_semantics_enabled_callback = [&](Dart_NativeArguments args) {
    bool enabled = true;
    auto handle = Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_FALSE(Dart_IsError(handle));
    ASSERT_FALSE(enabled);
    notify_semantics_enabled_latch.Signal();
  };
  notify_semantics_enabled_latch.Wait();

  // Prepare to NotifyAccessibilityFeatures call
  fml::AutoResetWaitableEvent notify_features_latch;
  notify_accessibility_features_callback = [&](Dart_NativeArguments args) {
    bool enabled = true;
    auto handle = Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_FALSE(Dart_IsError(handle));
    ASSERT_FALSE(enabled);
    notify_features_latch.Signal();
  };

  // Enable semantics. Wait for NotifySemanticsEnabled(true).
  fml::AutoResetWaitableEvent notify_semantics_enabled_latch_2;
  notify_semantics_enabled_callback = [&](Dart_NativeArguments args) {
    bool enabled = false;
    auto handle = Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_FALSE(Dart_IsError(handle));
    ASSERT_TRUE(enabled);
    notify_semantics_enabled_latch_2.Signal();
  };
  auto result = FlutterEngineUpdateSemanticsEnabled(engine.get(), true);
  ASSERT_EQ(result, FlutterEngineResult::kSuccess);
  notify_semantics_enabled_latch_2.Wait();

  // Wait for initial accessibility features (reduce_motion == false)
  notify_features_latch.Wait();

  // Set accessibility features: (reduce_motion == true)
  fml::AutoResetWaitableEvent notify_features_latch_2;
  notify_accessibility_features_callback = [&](Dart_NativeArguments args) {
    bool enabled = false;
    auto handle = Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_FALSE(Dart_IsError(handle));
    ASSERT_TRUE(enabled);
    notify_features_latch_2.Signal();
  };
  result = FlutterEngineUpdateAccessibilityFeatures(
      engine.get(), kFlutterAccessibilityFeatureReduceMotion);
  ASSERT_EQ(result, FlutterEngineResult::kSuccess);
  notify_features_latch_2.Wait();

  // Wait for UpdateSemantics callback on platform (current) thread.
  signal_native_latch.Wait();
  fml::MessageLoop::GetCurrent().RunExpiredTasksNow();
  semantics_node_latch.Wait();
  semantics_action_latch.Wait();
  ASSERT_EQ(4, node_count);
  ASSERT_EQ(1, node_batch_end_count);
  ASSERT_EQ(1, action_count);
  ASSERT_EQ(1, action_batch_end_count);

  // Dispatch a tap to semantics node 42. Wait for NotifySemanticsAction.
  fml::AutoResetWaitableEvent notify_semantics_action_latch;
  notify_semantics_action_callback = [&](Dart_NativeArguments args) {
    int64_t node_id = 0;
    Dart_GetNativeIntegerArgument(args, 0, &node_id);
    ASSERT_EQ(42, node_id);

    int64_t action_id;
    auto handle = Dart_GetNativeIntegerArgument(args, 1, &action_id);
    ASSERT_FALSE(Dart_IsError(handle));
    ASSERT_EQ(static_cast<int32_t>(flutter::SemanticsAction::kTap), action_id);

    Dart_Handle semantic_args = Dart_GetNativeArgument(args, 2);
    int64_t data;
    Dart_Handle dart_int = Dart_ListGetAt(semantic_args, 0);
    Dart_IntegerToInt64(dart_int, &data);
    ASSERT_EQ(2, data);

    dart_int = Dart_ListGetAt(semantic_args, 1);
    Dart_IntegerToInt64(dart_int, &data);
    ASSERT_EQ(1, data);
    notify_semantics_action_latch.Signal();
  };
  std::vector<uint8_t> bytes({2, 1});
  result = FlutterEngineDispatchSemanticsAction(
      engine.get(), 42, kFlutterSemanticsActionTap, &bytes[0], bytes.size());
  ASSERT_EQ(result, FlutterEngineResult::kSuccess);
  notify_semantics_action_latch.Wait();

  // Disable semantics. Wait for NotifySemanticsEnabled(false).
  fml::AutoResetWaitableEvent notify_semantics_enabled_latch_3;
  notify_semantics_enabled_callback = [&](Dart_NativeArguments args) {
    bool enabled = true;
    Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_FALSE(enabled);
    notify_semantics_enabled_latch_3.Signal();
  };
  result = FlutterEngineUpdateSemanticsEnabled(engine.get(), false);
  ASSERT_EQ(result, FlutterEngineResult::kSuccess);
  notify_semantics_enabled_latch_3.Wait();
}

}  // namespace testing
}  // namespace flutter

// NOLINTEND(clang-analyzer-core.StackAddressEscape)
