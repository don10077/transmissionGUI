import sys

path = sys.argv[1]
data = bytearray(open(path, 'rb').read())

def count_replace(needle, repl, cap=99):
    global data
    n = 0
    start = 0
    while n < cap:
        i = data.find(needle, start)
        if i == -1:
            break
        data[i:i + len(needle)] = repl
        n += 1
        start = i + len(repl)
    return n

# 1) restore stripped 'http:' in tracker URLs: header digits + ':' + content '//'
#    (scan manually: find '://' preceded by digits)
n = 0
start = 0
while n < 99:
    i = data.find(b'://', start)
    if i == -1:
        break
    # check digits before
    j = i - 1
    ok = False
    while j >= 0 and 48 <= data[j] <= 57:
        j -= 1
        ok = True
    # ensure the char before digits is a token boundary (e/d/i/l or start)
    if ok and (j < 0 or data[j] in (b'e'[0], b'd'[0], b'i'[0], b'l'[0])):
        data[i + 1:i + 1] = b'http:'
        n += 1
        start = i + 6
    else:
        start = i + 1
print('scheme fixes:', n)

# 2) comment + publisher-url lengths
n = count_replace(b'41:https://kinozaltv.life/details.php?id=2160453',
                  b'45:https://kinozaltv.life/details.php?id=2160453')
print('url41->45 fixes:', n)

# 3) publisher length
n = count_replace(b'10:kinozaltv.life', b'14:kinozaltv.life')
print('pub10->14 fixes:', n)

out = sys.argv[2] if len(sys.argv) > 2 else 'D:\\Work\\transmissionGUI\\tmp_repaired.torrent'
open(out, 'wb').write(bytes(data))
print('written', out, len(data))







