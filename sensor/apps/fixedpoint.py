def float2fixed(a,y):
    return int(round(a*pow(2,y))) 

def float2fixedBinary(a,x,y):
    #Set up fractional part in fixed point
    f = float2fixed(a,y)
    s = x+y
    binStr = '{0:0{width}b}'.format(f,width=s)
    #If -ve the - symbol is kept at the start of the string replace this
    return binStr.replace('-', '1')

def fix2float(a,y):
    return float(a/pow(2,y))

a = 2.4
b = float2fixedBinary(a,16,16)
print(b)
print(fix2float(b,16))
