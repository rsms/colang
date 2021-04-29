; Describes generic IR
;
(name     Generic)
(addrSize 4)
(regSize  4)
(intSize  4)
;
; Available operator flags:
;   ZeroWidth           purely symbolic; no actual I/O.
;   Constant            true if the value is a constant. Value in aux
;   Commutative         commutative on its first 2 arguments (e.g. addition; x+y==y+x)
;   ResultInArg0        output of v and v.args[0] must be allocated to the same register.
;   ResultNotInArgs     outputs must not be allocated to the same registers as inputs
;   Rematerializeable   register allocator can recompute value instead of spilling/restoring.
;   ClobberFlags        this op clobbers flags register
;   Call                is a function call
;   NilCheck            this op is a nil check on arg0
;   FaultOnNilArg0      this op will fault if arg0 is nil (and aux encodes a small offset)
;   FaultOnNilArg1      this op will fault if arg1 is nil (and aux encodes a small offset)
;   UsesScratch         this op requires scratch memory space
;   HasSideEffects      for "reasons", not to be eliminated.  E.g., atomic store.
;   Generic             generic op
;
; Types:
;   bool     bit or byte sized integer
;   i8       sign-agnostic 8-bit integer
;   s8       signed 8-bit integer
;   u8       unsigned 8-bit integer
;   i16      sign-agnostic 16-bit integer
;   s16      signed 16-bit integer
;   u16      unsigned 16-bit integer
;   i32      sign-agnostic 32-bit integer
;   s32      signed 32-bit integer
;   u32      unsigned 32-bit integer
;   i64      sign-agnostic 64-bit integer
;   s64      signed 64-bit integer
;   u64      unsigned 64-bit integer
;   addr     address (address-sized integer)
;   f32      32-bit floating point number
;   f64      64-bit floating point number
;   mem      memory location
;   nil      void
;
(ops
  (Nil ()->() ZeroWidth)
  (Phi ()->() ZeroWidth) ; select an argument based on which predecessor block we came from
  (Arg ()->() (aux i32))
  ;
  ; Constant values. Stored in IRValue.aux
  (ConstBool  () -> bool  Constant  (aux bool))  ; aux is 0=false, 1=true
  (ConstI8    () -> i8    Constant  (aux i8))  ; aux is sign-extended 8 bits
  (ConstI16   () -> i16   Constant  (aux i16)) ; aux is sign-extended 16 bits
  (ConstI32   () -> i32   Constant  (aux i32)) ; aux is sign-extended 32 bits
  (ConstI64   () -> i64   Constant  (aux i64)) ; aux is Int64
  (ConstF32   () -> f32   Constant  (aux i32))
  (ConstF64   () -> f64   Constant  (aux i64))
  ;
  ; ---------------------------------------------------------------------
  ; 2-input arithmetic. Types must be consistent.
  ; Add for example must take two values of the same type and produce that same type.
  ;
  ; arg0 + arg1 ; sign-agnostic addition
  (AddI8   (i8  i8)  -> i8   Commutative  ResultInArg0)
  (AddI16  (i16 i16) -> i16  Commutative  ResultInArg0)
  (AddI32  (i32 i32) -> i32  Commutative  ResultInArg0)
  (AddI64  (i64 i64) -> i64  Commutative  ResultInArg0)
  (AddF32  (f32 f32) -> f32  Commutative  ResultInArg0)
  (AddF64  (f64 f64) -> f64  Commutative  ResultInArg0)
  ;
  ; arg0 - arg1 ; sign-agnostic subtraction
  (SubI8   (i8  i8)  -> i8   ResultInArg0)
  (SubI16  (i16 i16) -> i16  ResultInArg0)
  (SubI32  (i32 i32) -> i32  ResultInArg0)
  (SubI64  (i64 i64) -> i64  ResultInArg0)
  (SubF32  (f32 f32) -> f32  ResultInArg0)
  (SubF64  (f64 f64) -> f64  ResultInArg0)
  ;
  ; arg0 * arg1 ; sign-agnostic multiplication
  (MulI8   (i8  i8)  -> i8   Commutative  ResultInArg0)
  (MulI16  (i16 i16) -> i16  Commutative  ResultInArg0)
  (MulI32  (i32 i32) -> i32  Commutative  ResultInArg0)
  (MulI64  (i64 i64) -> i64  Commutative  ResultInArg0)
  (MulF32  (f32 f32) -> f32  Commutative  ResultInArg0)
  (MulF64  (f64 f64) -> f64  Commutative  ResultInArg0)
  ;
  ; arg0 / arg1 ; division
  (DivS8   (s8  s8)  -> s8   ResultInArg0) ; signed (result is truncated toward zero)
  (DivU8   (u8  u8)  -> u8   ResultInArg0) ; unsigned (result is floored)
  (DivS16  (s16 s16) -> s16  ResultInArg0)
  (DivU16  (u16 u16) -> u16  ResultInArg0)
  (DivS32  (s32 s32) -> s32  ResultInArg0)
  (DivU32  (u32 u32) -> u32  ResultInArg0)
  (DivS64  (s64 s64) -> s64  ResultInArg0)
  (DivU64  (u64 u64) -> u64  ResultInArg0)
  (DivF32  (f32 f32) -> f32  ResultInArg0)
  (DivF64  (f64 f64) -> f64  ResultInArg0)
  ;
  ; arg0 % arg1 ; remainder
  ; [1] For Mod16, Mod32 and Mod64, AuxInt non-zero means that
  ;     the divisor has been proved to be not -1.
  (ModS8   (s8  s8)  -> s8)
  (ModU8   (u8  u8)  -> u8)
  (ModS16  (s16 s16) -> s16 (aux bool)) ; [1]
  (ModU16  (u16 u16) -> u16)
  (ModS32  (s32 s32) -> s32 (aux bool)) ; [1]
  (ModU32  (u32 u32) -> u32)
  (ModS64  (s64 s64) -> s64 (aux bool)) ; [1]
  (ModU64  (u64 u64) -> u64)
  ;
  ; arg0 & arg1 ; bitwise AND
  (And8    (i8  i8)  -> i8   Commutative)
  (And16   (i16 i16) -> i16  Commutative)
  (And32   (i32 i32) -> i32  Commutative)
  (And64   (i64 i64) -> i64  Commutative)
  ;
  ; arg0 | arg1 ; bitwise OR
  (Or8    (i8  i8)  -> i8    Commutative)
  (Or16   (i16 i16) -> i16   Commutative)
  (Or32   (i32 i32) -> i32   Commutative)
  (Or64   (i64 i64) -> i64   Commutative)
  ;
  ; arg0 ^ arg1 ; bitwise XOR
  (Xor8    (i8  i8)  -> i8   Commutative)
  (Xor16   (i16 i16) -> i16  Commutative)
  (Xor32   (i32 i32) -> i32  Commutative)
  (Xor64   (i64 i64) -> i64  Commutative)
  ;
  ; For shifts, AxB means the shifted value has A bits and the shift amount has B bits.
  ; Shift amounts are considered unsigned.
  ; If arg1 is known to be less than the number of bits in arg0, then aux may be set to 1.
  ; According to the Go assembler, this enables better code generation on some platforms.
  ;
  ; arg0 << arg1 ; sign-agnostic shift left
  (ShLI8x8    (i8  u8)  -> i8   (aux bool))
  (ShLI8x16   (i8  u16) -> i8   (aux bool))
  (ShLI8x32   (i8  u32) -> i8   (aux bool))
  (ShLI8x64   (i8  u64) -> i8   (aux bool))
  (ShLI16x8   (i16 u8)  -> i16  (aux bool))
  (ShLI16x16  (i16 u16) -> i16  (aux bool))
  (ShLI16x32  (i16 u32) -> i16  (aux bool))
  (ShLI16x64  (i16 u64) -> i16  (aux bool))
  (ShLI32x8   (i32 u8)  -> i32  (aux bool))
  (ShLI32x16  (i32 u16) -> i32  (aux bool))
  (ShLI32x32  (i32 u32) -> i32  (aux bool))
  (ShLI32x64  (i32 u64) -> i32  (aux bool))
  (ShLI64x8   (i64 u8)  -> i64  (aux bool))
  (ShLI64x16  (i64 u16) -> i64  (aux bool))
  (ShLI64x32  (i64 u32) -> i64  (aux bool))
  (ShLI64x64  (i64 u64) -> i64  (aux bool))
  ;
  ; arg0 >> arg1 ; sign-replicating (arithmetic) shift right
  (ShRS8x8    (s8  u8)  -> s8   (aux bool))
  (ShRS8x16   (s8  u16) -> s8   (aux bool))
  (ShRS8x32   (s8  u32) -> s8   (aux bool))
  (ShRS8x64   (s8  u64) -> s8   (aux bool))
  (ShRS16x8   (s16 u8)  -> s16  (aux bool))
  (ShRS16x16  (s16 u16) -> s16  (aux bool))
  (ShRS16x32  (s16 u32) -> s16  (aux bool))
  (ShRS16x64  (s16 u64) -> s16  (aux bool))
  (ShRS32x8   (s32 u8)  -> s32  (aux bool))
  (ShRS32x16  (s32 u16) -> s32  (aux bool))
  (ShRS32x32  (s32 u32) -> s32  (aux bool))
  (ShRS32x64  (s32 u64) -> s32  (aux bool))
  (ShRS64x8   (s64 u8)  -> s64  (aux bool))
  (ShRS64x16  (s64 u16) -> s64  (aux bool))
  (ShRS64x32  (s64 u32) -> s64  (aux bool))
  (ShRS64x64  (s64 u64) -> s64  (aux bool))
  ;
  ; arg0 >> arg1 (aka >>>) ; zero-replicating (logical) shift right
  (ShRU8x8    (u8  u8)  -> u8   (aux bool))
  (ShRU8x16   (u8  u16) -> u8   (aux bool))
  (ShRU8x32   (u8  u32) -> u8   (aux bool))
  (ShRU8x64   (u8  u64) -> u8   (aux bool))
  (ShRU16x8   (u16 u8)  -> u16  (aux bool))
  (ShRU16x16  (u16 u16) -> u16  (aux bool))
  (ShRU16x32  (u16 u32) -> u16  (aux bool))
  (ShRU16x64  (u16 u64) -> u16  (aux bool))
  (ShRU32x8   (u32 u8)  -> u32  (aux bool))
  (ShRU32x16  (u32 u16) -> u32  (aux bool))
  (ShRU32x32  (u32 u32) -> u32  (aux bool))
  (ShRU32x64  (u32 u64) -> u32  (aux bool))
  (ShRU64x8   (u64 u8)  -> u64  (aux bool))
  (ShRU64x16  (u64 u16) -> u64  (aux bool))
  (ShRU64x32  (u64 u32) -> u64  (aux bool))
  (ShRU64x64  (u64 u64) -> u64  (aux bool))
  ;
  ; ---------------------------------------------------------------------
  ; 2-input comparisons
  ;
  ; arg0 == arg1 ; sign-agnostic compare equal
  (EqI8   (i8 i8)   -> bool    Commutative)
  (EqI16  (i16 i16) -> bool    Commutative)
  (EqI32  (i32 i32) -> bool    Commutative)
  (EqI64  (i64 i64) -> bool    Commutative)
  (EqF32  (f32 f32) -> bool    Commutative)
  (EqF64  (f64 f64) -> bool    Commutative)
  ;
  ; arg0 != arg1 ; sign-agnostic compare unequal
  (NEqI8   (i8 i8)   -> bool   Commutative)
  (NEqI16  (i16 i16) -> bool   Commutative)
  (NEqI32  (i32 i32) -> bool   Commutative)
  (NEqI64  (i64 i64) -> bool   Commutative)
  (NEqF32  (f32 f32) -> bool   Commutative)
  (NEqF64  (f64 f64) -> bool   Commutative)
  ;
  ; arg0 < arg1 ; less than
  (LessS8    (s8 s8) -> bool) ; signed
  (LessU8    (u8 u8) -> bool) ; unsigned
  (LessS16   (s16 s16) -> bool)
  (LessU16   (u16 u16) -> bool)
  (LessS32   (s32 s32) -> bool)
  (LessU32   (u32 u32) -> bool)
  (LessS64   (s64 s64) -> bool)
  (LessU64   (u64 u64) -> bool)
  (LessF32   (f32 f32) -> bool)
  (LessF64   (f64 f64) -> bool)
  ;
  ; arg0 > arg1 ; greater than
  (GreaterS8   (s8 s8) -> bool) ; signed
  (GreaterU8   (u8 u8) -> bool) ; unsigned
  (GreaterS16  (s16 s16) -> bool)
  (GreaterU16  (u16 u16) -> bool)
  (GreaterS32  (s32 s32) -> bool)
  (GreaterU32  (u32 u32) -> bool)
  (GreaterS64  (s64 s64) -> bool)
  (GreaterU64  (u64 u64) -> bool)
  (GreaterF32  (f32 f32) -> bool)
  (GreaterF64  (f64 f64) -> bool)
  ;
  ; arg0 <= arg1 ; less than or equal
  (LEqS8   (s8 s8) -> bool) ; signed
  (LEqU8   (u8 u8) -> bool) ; unsigned
  (LEqS16  (s16 s16) -> bool)
  (LEqU16  (u16 u16) -> bool)
  (LEqS32  (s32 s32) -> bool)
  (LEqU32  (u32 u32) -> bool)
  (LEqS64  (s64 s64) -> bool)
  (LEqU64  (u64 u64) -> bool)
  (LEqF32  (f32 f32) -> bool)
  (LEqF64  (f64 f64) -> bool)
  ;
  ; arg0 <= arg1 ; greater than or equal
  (GEqS8   (s8 s8) -> bool) ; signed
  (GEqU8   (u8 u8) -> bool) ; unsigned
  (GEqS16  (s16 s16) -> bool)
  (GEqU16  (u16 u16) -> bool)
  (GEqS32  (s32 s32) -> bool)
  (GEqU32  (u32 u32) -> bool)
  (GEqS64  (s64 s64) -> bool)
  (GEqU64  (u64 u64) -> bool)
  (GEqF32  (f32 f32) -> bool)
  (GEqF64  (f64 f64) -> bool)
  ;
  ; boolean op boolean
  (AndB (bool bool) -> bool  Commutative) ; arg0 && arg1 (not shortcircuited)
  (OrB  (bool bool) -> bool  Commutative) ; arg0 || arg1 (not shortcircuited)
  (EqB  (bool bool) -> bool  Commutative) ; arg0 == arg1
  (NEqB (bool bool) -> bool  Commutative) ; arg0 != arg1
  ;
  ; ---------------------------------------------------------------------
  ; 1-input
  ;
  ; !arg0 ; boolean not
  (NotB  bool -> bool)
  ;
  ; -arg0 ; negation
  (NegI8  i8  -> i8)
  (NegI16 i16 -> i16)
  (NegI32 i32 -> i32)
  (NegI64 i64 -> i64)
  (NegF32 f32 -> f32)
  (NegF64 f64 -> f64)
  ;
  ; ^arg0 ; bitwise complement
  ; m ^ x  with m = "all bits set to 1" for unsigned x
  ;        and  m = -1 for signed x
  (Compl8  i8 -> i8)
  (Compl16 i16 -> i16)
  (Compl32 i32 -> i32)
  (Compl64 i64 -> i64)
  ;
  ; ---------------------------------------------------------------------
  ; Conversions
  ; Note: For the generic "base" arch, all conversion ops must start with "Conv" in
  ;       order for the gen_opts.py program to map them in the conversion table.
  ; Note: Sign conversion, i.e. i32 -> u32 happens by simply interpreting the number
  ;       differently. There are no signed conversion ops.
  ;
  ; extensions
  (ConvS8to16     s8  -> s16)
  (ConvS8to32     s8  -> s32)
  (ConvS8to64     s8  -> s64)
  (ConvU8to16     u8  -> u16)
  (ConvU8to32     u8  -> u32)
  (ConvU8to64     u8  -> u64)
  (ConvS16to32    s16 -> s32)
  (ConvS16to64    s16 -> s64)
  (ConvU16to32    u16 -> u32)
  (ConvU16to64    u16 -> u64)
  (ConvS32to64    s32 -> s64)
  (ConvU32to64    u32 -> u64)
  ;
  ; truncations
  (ConvI16to8     i16 -> i8    Lossy)
  (ConvI32to8     i32 -> i8    Lossy)
  (ConvI32to16    i32 -> i16   Lossy)
  (ConvI64to8     i64 -> i8    Lossy)
  (ConvI64to16    i64 -> i16   Lossy)
  (ConvI64to32    i64 -> i32   Lossy)
  ;
  ; conversions
  (ConvS32toF32  s32 -> f32    Lossy)
  (ConvS32toF64  s32 -> f64)
  (ConvS64toF32  s64 -> f32    Lossy)
  (ConvS64toF64  s64 -> f64    Lossy)
  (ConvU32toF32  u32 -> f32    Lossy)
  (ConvU32toF64  u32 -> f64)
  (ConvU64toF32  u64 -> f32    Lossy)
  (ConvU64toF64  u64 -> f64    Lossy)
  (ConvF32toF64  f32 -> f64)
  (ConvF32toS32  f32 -> s32    Lossy)
  (ConvF32toS64  f32 -> s64    Lossy)
  (ConvF32toU32  f32 -> u32    Lossy)
  (ConvF32toU64  f32 -> u64    Lossy)
  (ConvF64toF32  f64 -> f32    Lossy)
  (ConvF64toS32  f64 -> s32    Lossy)
  (ConvF64toS64  f64 -> s64    Lossy)
  (ConvF64toU32  f64 -> u32    Lossy)
  (ConvF64toU64  f64 -> u64    Lossy)


) ; ops
