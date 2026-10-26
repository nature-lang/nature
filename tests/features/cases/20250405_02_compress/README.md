##  [nax lang](https://github.com/naxlang/nax)  compress package 

### Example

Currently, only compression and decompression in TGZ format are supported

```
import compress.tgz

tgz.verbose = true

tgz.encode('mockdir', 'mockdir.tar.gz', ['sub', 'nice.n', 'test.txt'])
println('encode success')

tgz.decode('mockdir', 'mockdir.tar.gz')
println('decode success')
```

### License

MIT