import glob
import os
import sys

def EscapePath(path):
  return path.replace(' ', '\\ ')


def List(path):
  output = []

  if not os.path.isdir(path):
    for file in glob.glob(path):
      output.append(file)
    return output

  output.extend(path)
  return output


def Expand(inputs):
  output = []
  for input in inputs:
    list = List(input)
    output.extend(list)
  return output


def DoMain(argv):
  escaped = [EscapePath(x) for x in argv]
  expanded = Expand(escaped)
  return '\n'.join([x for x in expanded])


def main(argv):
  try:
    result = DoMain(argv[1:])
  except e:
    print >> sys.stderr, e
    return 1
  if result:
    print result
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv))
