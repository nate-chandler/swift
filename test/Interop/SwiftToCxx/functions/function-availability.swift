// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend %s -typecheck -module-name Functions -emit-cxx-header-path %t/functions.h
// RUN: %FileCheck %s < %t/functions.h

// RUN: %check-interop-cxx-header-in-clang(%t/functions.h)

// CHECK-LABEL: namespace Functions {

// CHECK-LABEL: namespace _impl {

// CHECK: extern "C" void $s9Functions16alwaysDeprecatedyyF(void) noexcept SWIFT_CALL SWIFT_DEPRECATED; // alwaysDeprecated()
// CHECK: extern "C" void $s9Functions19alwaysDeprecatedTwoyyF(void) noexcept SWIFT_CALL SWIFT_DEPRECATED_MSG("it should not be used"); // alwaysDeprecatedTwo()
// CHECK: extern "C" void $s9Functions17alwaysUnavailableyyF(void) noexcept SWIFT_CALL SWIFT_UNAVAILABLE; // alwaysUnavailable()
// CHECK: extern "C" void $s9Functions24alwaysUnavailableMessageyyF(void) noexcept SWIFT_CALL SWIFT_UNAVAILABLE_MSG("stuff happened"); // alwaysUnavailableMessage()
// CHECK: extern "C" void $s9Functions22singlePlatAvailabilityyyF(void) noexcept SWIFT_CALL SWIFT_AVAILABILITY(macos,introduced=11); // singlePlatAvailability()

// CHECK: }

// CHECK: inline void alwaysDeprecated(void) noexcept SWIFT_DEPRECATED {
@available(*, deprecated)
public func alwaysDeprecated() {}

// CHECK: inline void alwaysDeprecatedTwo(void) noexcept SWIFT_DEPRECATED_MSG("it should not be used")
@available(*, deprecated, message: "it should not be used")
public func alwaysDeprecatedTwo() {}

// CHECK: inline void alwaysUnavailable(void) noexcept SWIFT_UNAVAILABLE
@available(*, unavailable)
public func alwaysUnavailable() {}

// CHECK: inline void alwaysUnavailableMessage(void) noexcept SWIFT_UNAVAILABLE_MSG("stuff happened")
@available(*, unavailable, message: "stuff happened")
public func alwaysUnavailableMessage() {}

// CHECK: inline void singlePlatAvailability(void) noexcept SWIFT_AVAILABILITY(macos,introduced=11)
@available(macOS 11, *)
public func singlePlatAvailability() {}
