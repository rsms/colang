-- defines a factorial function
function fact(n)
  if n == 0 then
    return 1
  else
    return n * fact(n-1)
  end
end

print("enter a number:")
a = io.read("*number")
if a == nil then
  print("no number entered; defaulting to 4")
  a = 4
end

print(fact(a))
