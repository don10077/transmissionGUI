import sys

data = open(sys.argv[1], 'rb').read()
n = len(data)
print("file size:", n)

def is_num(c):
    return 48 <= c <= 57

def header_at(i):
    # return (length, content_start) if digits+':' at i (<=10 digits)
    j = i
    while j < n and is_num(data[j]) and j - i < 10:
        j += 1
    if j == i or j >= n or data[j] != 58:
        return None
    return int(data[i:j].decode('ascii'), 10), j + 1

# Walk structure, tracking stack of container kinds, show what parse "believes"
stack = []
pos = 0
errors = []
shown = 0
while pos < n:
    c = data[pos]
    if c == 101:  # 'e'
        if not stack:
            errors.append((pos, "e with empty stack"))
            break
        stack.pop()
        pos += 1
        continue
    if c == 108:  # 'l'
        if not stack:
            errors.append((pos, "l with empty stack"))
            break
        if stack[-1] != 'list':
            errors.append((pos, "l closing non-list (top=%r)" % stack[-1]))
            break
        stack.pop()
        pos += 1
        continue
    if c == 100:  # 'd'
        stack.append('dict')
        pos += 1
        continue
    if is_num(c):
        h = header_at(pos)
        if h is None:
            errors.append((pos, "bad number header"))
            break
        length, cs = h
        if cs + length > n:
            errors.append((pos, "string length %d overruns EOF (cs=%d)" % (length, cs)))
            break
        if stack and stack[-1] == 'dict':
            # key: must be followed by a token start
            after = data[cs + length] if cs + length < n else None
            if after is None or not (after == 101 or after == 105 or after == 108 or after == 100 or is_num(after)):
                errors.append((pos, "dict key 'end' not followed by token start (next=%r)" % chr(after) if after else "dict key overruns EOF"))
                break
        pos = cs + length
        continue
    # binary garbage where a token was expected
    errors.append((pos, "non-token byte 0x%02x (context=%r)" % (c, data[max(0,pos-20):pos+20])))
    break

print("walk stopped at pos", pos, "errors:", errors[:3])
for p, msg in errors[:3]:
    print("--- context at", p, "---")
    print("HEX:", ' '.join('%02x' % b for b in data[max(0,p-40):p+60]))
    print("ASC:", ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[max(0,p-40):p+60]))

# Show structure around 'piece layers' and tail
for needle in [b'piece layers', b'publisher']:
    i = data.find(needle)
    while i != -1:
        print("found %r at %d" % (needle, i))
        print("  HEX:", ' '.join('%02x' % b for b in data[max(0,i-30):i+60]))
        i = data.find(needle, i + 1)
        if i > 200000:
            break

# Tail structure walk: parse from first 'e' that closes root backwards? Instead:
# Show the last 200 bytes hex
print("--- last 200 bytes ---")
t = n - 200
print("HEX:", ' '.join('%02x' % b for b in data[t:]))
print("ASC:", ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[t:]))
