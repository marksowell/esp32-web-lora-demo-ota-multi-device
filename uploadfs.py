Import("env")

def after_build(source, target, env):
    print("Running FS Upload")
    env.Execute("pio run --target uploadfs")

env.AddPostAction("upload", after_build)
