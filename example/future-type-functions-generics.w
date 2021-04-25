# generics w required type Name
type Vec3(T) = (T, T, T)
type Tup3(Y) = (Y, Y, Y)
a Vec3(int) = (1,1,0)
b Bec3(float) = (1.0,1.0,0.0)
c = (1,1,0) # == a
d Tup3(float) = (1.0,1.0,0.0) # == b
