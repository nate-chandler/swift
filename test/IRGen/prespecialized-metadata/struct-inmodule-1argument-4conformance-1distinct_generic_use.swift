// RUN: %swift -target %module-target-future -emit-ir -prespecialize-generic-metadata %s | %FileCheck %s -DINT=i%target-ptrsize -DALIGNMENT=%target-alignment

// UNSUPPORTED: CPU=i386 && OS=ios
// UNSUPPORTED: CPU=armv7 && OS=ios
// UNSUPPORTED: CPU=armv7s && OS=ios

// CHECK: @"$s4main3BoxVyxGAA1SAAWP" = hidden constant [1 x i8*] [i8* bitcast (%swift.protocol_conformance_descriptor* @"$s4main3BoxVyxGAA1SAAMc" to i8*)], align [[ALIGNMENT]]
// CHECK: @"$s4main3BoxVyxGAA1RAAWP" = hidden constant [1 x i8*] [i8* bitcast (%swift.protocol_conformance_descriptor* @"$s4main3BoxVyxGAA1RAAMc" to i8*)], align [[ALIGNMENT]]
// CHECK: @"$s4main3BoxVyxGAA1QAAWP" = hidden constant [1 x i8*] [i8* bitcast (%swift.protocol_conformance_descriptor* @"$s4main3BoxVyxGAA1QAAMc" to i8*)], align [[ALIGNMENT]]
// CHECK: @"$s4main3BoxVyxGAA1PAAWP" = hidden constant [1 x i8*] [i8* bitcast (%swift.protocol_conformance_descriptor* @"$s4main3BoxVyxGAA1PAAMc" to i8*)], align [[ALIGNMENT]]
// CHECK: @"$sytN" = external global %swift.full_type
// CHECK: @"$sB[[INT]]_WV" = external global i8*, align [[ALIGNMENT]]
// CHECK: @"$s4main5ValueVyAA3BoxVySiGGMf" = internal constant <{ i8**, [[INT]], %swift.type_descriptor*, %swift.type*, i8**, i8**, i8**, i8**, i32{{(, \[4 x i8\])?}}, i64 }> <{ i8** @"$sB[[INT]]_WV", [[INT]] 512, %swift.type_descriptor* bitcast (<{ i32, i32, i32, i32, i32, i32, i32, i32, i32, i16, i16, i16, i16, i8, i8, i8, i8, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32 }>* @"$s4main5ValueVMn" to %swift.type_descriptor*), %swift.type* getelementptr inbounds (%swift.full_type, %swift.full_type* bitcast (<{ i8**, [[INT]], %swift.type_descriptor*, %swift.type*, i8**, i8**, i8**, i8**, i32{{(, \[4 x i8\])?}}, i64 }>* @"$s4main3BoxVySiGMf" to %swift.full_type*), i32 0, i32 1), i8** getelementptr inbounds ([1 x i8*], [1 x i8*]* @"$s4main3BoxVyxGAA1PAAWP", i32 0, i32 0), i8** getelementptr inbounds ([1 x i8*], [1 x i8*]* @"$s4main3BoxVyxGAA1QAAWP", i32 0, i32 0), i8** getelementptr inbounds ([1 x i8*], [1 x i8*]* @"$s4main3BoxVyxGAA1RAAWP", i32 0, i32 0), i8** getelementptr inbounds ([1 x i8*], [1 x i8*]* @"$s4main3BoxVyxGAA1SAAWP", i32 0, i32 0), i32 0{{(, \[4 x i8\] zeroinitializer)?}}, i64 3 }>, align [[ALIGNMENT]]
protocol P {}
protocol Q {}
protocol R {}
protocol S {}
extension Int : P {}
extension Int : Q {}
extension Int : R {}
extension Int : S {}
struct Value<First : P & Q & R & S> {
  let first: First
}

struct Box<Value : P & Q & R & S> : P & Q & R & S {
  let value: Value
}

@inline(never)
func consume<T>(_ t: T) {
  withExtendedLifetime(t) { t in
  }
}

// CHECK: define hidden swiftcc void @"$s4main4doityyF"() #{{[0-9]+}} {
// CHECK:   call swiftcc void @"$s4main7consumeyyxlF"(%swift.opaque* noalias nocapture %{{[0-9]+}}, %swift.type* getelementptr inbounds (%swift.full_type, %swift.full_type* bitcast (<{ i8**, [[INT]], %swift.type_descriptor*, %swift.type*, i8**, i8**, i8**, i8**, i32{{(, \[4 x i8\])?}}, i64 }>* @"$s4main5ValueVyAA3BoxVySiGGMf" to %swift.full_type*), i32 0, i32 1))
// CHECK: }
func doit() {
  consume( Value(first: Box(value: 13)) )
}
doit()

// CHECK: ; Function Attrs: noinline nounwind
// CHECK: define hidden swiftcc %swift.metadata_response @"$s4main5ValueVMa"([[INT]], i8**) #{{[0-9]+}} {
// CHECK: entry:
// CHECK:   [[ERASED_ARGUMENT_BUFFER:%[0-9]+]] = bitcast i8** %1 to i8*
// CHECK:   br label %[[TYPE_COMPARISON_1:[0-9]+]]
// CHECK: [[TYPE_COMPARISON_1]]:
// CHECK:   [[ERASED_TYPE_ADDRESS:%[0-9]+]] = getelementptr i8*, i8** %1, i64 0
// CHECK:   %"load argument at index 0 from buffer" = load i8*, i8** [[ERASED_TYPE_ADDRESS]]
// CHECK:   [[EQUAL_TYPE:%[0-9]+]] = icmp eq i8* bitcast (%swift.type* getelementptr inbounds (%swift.full_type, %swift.full_type* bitcast (<{ i8**, [[INT]], %swift.type_descriptor*, %swift.type*, i8**, i8**, i8**, i8**, i32{{(, \[4 x i8\])?}}, i64 }>* @"$s4main3BoxVySiGMf" to %swift.full_type*), i32 0, i32 1) to i8*), %"load argument at index 0 from buffer"
// CHECK:   [[EQUAL_TYPES:%[0-9]+]] = and i1 true, [[EQUAL_TYPE]]
// CHECK:   br i1 [[EQUAL_TYPES]], label %[[EXIT_PRESPECIALIZED:[0-9]+]], label %[[EXIT_NORMAL:[0-9]+]]
// CHECK: [[EXIT_PRESPECIALIZED]]:
// CHECK:   ret %swift.metadata_response { %swift.type* getelementptr inbounds (%swift.full_type, %swift.full_type* bitcast (<{ i8**, [[INT]], %swift.type_descriptor*, %swift.type*, i8**, i8**, i8**, i8**, i32{{(, \[4 x i8\])?}}, i64 }>* @"$s4main5ValueVyAA3BoxVySiGGMf" to %swift.full_type*), i32 0, i32 1), [[INT]] 0 }
// CHECK: [[EXIT_NORMAL]]:
// CHECK:   {{%[0-9]+}} = call swiftcc %swift.metadata_response @swift_getGenericMetadata([[INT]] %0, i8* [[ERASED_ARGUMENT_BUFFER]], %swift.type_descriptor* bitcast (<{ i32, i32, i32, i32, i32, i32, i32, i32, i32, i16, i16, i16, i16, i8, i8, i8, i8, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32 }>* @"$s4main5ValueVMn" to %swift.type_descriptor*)) #{{[0-9]+}}
// CHECK:   ret %swift.metadata_response {{%[0-9]+}}
// CHECK: }
