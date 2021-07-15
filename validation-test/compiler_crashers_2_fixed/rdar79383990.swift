// RUN: %target-swift-frontend %s -emit-silgen
// REQUIRES: objc_interop

import Foundation

func test(s: NSBackgroundActivityScheduler) async {
    _ = await s.schedule()
}
