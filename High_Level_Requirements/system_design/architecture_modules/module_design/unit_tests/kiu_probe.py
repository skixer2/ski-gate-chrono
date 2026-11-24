import inspect
import kiutils.schematic as s

names = [n for n in dir(s) if "Instance" in n or "Project" in n]
print("classes:", names)
print()
for n in names:
    obj = getattr(s, n)
    if inspect.isclass(obj):
        try:
            print("%s.__init__%s" % (n, inspect.signature(obj.__init__)))
        except (ValueError, TypeError) as e:
            print("%s: %s" % (n, e))
print()
# show the to_sexpr of the project instance so I can see the expected fields
for n in names:
    obj = getattr(s, n)
    if inspect.isclass(obj) and "ProjectInstance" in n:
        src = inspect.getsource(obj)
        print("=== %s source (first 60 lines) ===" % n)
        for ln in src.splitlines()[:60]:
            print(ln)
