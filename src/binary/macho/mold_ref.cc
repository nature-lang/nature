// promat 这是实现 macho 链接器参考文件，这是开源项目 mold 中代码，使用 c++ 实现，我需要实现一个 c 语言版本的 macho 链接器，基本完全参考仿照该文件实现
// promat 这是实现 macho 链接器参考文件，这是开源项目 mold 中代码，使用 c++ 实现，我需要实现一个 c 语言版本的 macho 链接器，基本完全参考仿照该文件实现
// promat 这是实现 macho 链接器参考文件，这是开源项目 mold 中代码，使用 c++ 实现，我需要实现一个 c 语言版本的 macho 链接器，基本完全参考仿照该文件实现
// promat 这是实现 macho 链接器参考文件，这是开源项目 mold 中代码，使用 c++ 实现，我需要实现一个 c 语言版本的 macho 链接器，基本完全参考仿照该文件实现



// 文件 main.cc
#include "mold.h"
#include "../common/archive-file.h"
#include "../common/output-file.h"
#include "../common/sha.h"

#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <tbb/concurrent_vector.h>
#include <tbb/global_control.h>
#include <tbb/parallel_for_each.h>
#include <tbb/parallel_sort.h>

#ifndef _WIN32
# include <sys/mman.h>
# include <sys/time.h>
#endif

namespace mold::macho {
    static std::pair<std::string_view, std::string_view>
    split_string(std::string_view str, char sep) {
        size_t pos = str.find(sep);
        if (pos == str.npos)
            return {str, ""};
        return {str.substr(0, pos), str.substr(pos + 1)};
    }

    template<typename E>
    static bool has_lto_obj(Context<E> &ctx) {
        for (ObjectFile<E> *file: ctx.objs)
            if (file->lto_module)
                return true;
        return false;
    }

    template<typename E>
    static void resolve_symbols(Context<E> &ctx) {
        // Timer t(ctx, "resolve_symbols");

        std::vector<InputFile<E> *> files;
        append(files, ctx.objs);
        append(files, ctx.dylibs);

        tbb::parallel_for_each(files, [&](InputFile<E> *file) {
            file->resolve_symbols(ctx);
        });

        if (InputFile<E> *file = ctx.arg.entry->file)
            file->is_alive = true;

        // Mark reachable object files
        std::vector<ObjectFile<E> *> live_objs;
        for (ObjectFile<E> *file: ctx.objs)
            if (file->is_alive)
                live_objs.push_back(file);

        for (i64 i = 0; i < live_objs.size(); i++) {
            live_objs[i]->mark_live_objects(ctx, [&](ObjectFile<E> *file) {
                live_objs.push_back(file);
            });
        }

        // Remove symbols of eliminated files.
        tbb::parallel_for_each(files, [&](InputFile<E> *file) {
            if (!file->is_alive)
                file->clear_symbols();
        });

        // Redo symbol resolution because extracting object files from archives
        // may raise the priority of symbols defined by the object file.
        tbb::parallel_for_each(files, [&](InputFile<E> *file) {
            if (file->is_alive)
                file->resolve_symbols(ctx);
        });


        // 移除 is_alive 为 false 的文件
        std::erase_if(ctx.objs, [](InputFile<E> *file) { return !file->is_alive; });
        std::erase_if(ctx.dylibs, [](InputFile<E> *file) { return !file->is_alive; });

        // 移除后重新分配 dylib_idx
        for (i64 i = 1; DylibFile<E> *file: ctx.dylibs)
            if (file->dylib_idx != BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE)
                file->dylib_idx = i++;
    }

    template<typename E>
    static void compute_import_export(Context<E> &ctx) {
        // Compute is_imported and is_exported values
        // tbb::parallel_for_each(ctx.objs, [&](ObjectFile<E> *file) {
        //     for (Symbol<E> *sym: file->syms) {
        //         if (!sym || sym->visibility != SCOPE_GLOBAL) {
        //             continue;
        //         }

        //         // If we are using a dylib symbol, we need to import it.
        //         if (sym->file && sym->file->is_dylib) {
        //             std::scoped_lock lock(sym->mu);
        //             sym->is_imported = true;
        //         }
        //     }
        // });

        for (ObjectFile<E> *file: ctx.objs) {
            for (Symbol<E> *sym: file->syms) {
                if (!sym || sym->visibility != SCOPE_GLOBAL) {
                    continue;
                }

                // If we are using a dylib symbol, we need to import it.
                if (sym->file && sym->file->is_dylib) {
                    std::scoped_lock lock(sym->mu);
                    sym->is_imported = true;
                }
            }
        }
    }

    template<typename E>
    static void create_internal_file(Context<E> &ctx) {
        // 创建内部对象文件
        ObjectFile<E> *obj = new ObjectFile<E>;
        obj->is_alive = true;
        obj->mach_syms = obj->mach_syms2;
        ctx.obj_pool.emplace_back(obj);
        ctx.objs.push_back(obj);
        ctx.internal_obj = obj;


        // 将 __dyld_private 和 ___dso_handle 添加到内部对象文件
        auto add = [&](Symbol<E> *sym) {
            sym->file = obj; // 建立反向引用
            obj->syms.push_back(sym);
        };
        add(ctx.__dyld_private);
        add(ctx.___dso_handle);

        assert(ctx.output_type == MH_EXECUTE);
        add(ctx.__mh_execute_header);
        ctx.__mh_execute_header->visibility = SCOPE_GLOBAL;
        ctx.__mh_execute_header->is_exported = true;
        // 设置 __mh_execute_header 的 value 为 pagezero_size, pagezero_size 是 macho 文件的第一个 segment 的起始地址
        // 在 arm64 架构中，pagezero_size 的值为 0x100000000, 在 x86_64 架构中，pagezero_size 的值为 0x10000000
        ctx.__mh_execute_header->value = ctx.arg.pagezero_size;
    }

    template<typename E>
    static bool compare_segments(const std::unique_ptr<OutputSegment<E> > &a,
                                 const std::unique_ptr<OutputSegment<E> > &b) {
        // We want to sort output segments in the following order:
        // __TEXT, __DATA_CONST, __DATA, <other segments>, __LINKEDIT
        auto get_rank = [](std::string_view name) {
            if (name == "__TEXT")
                return 0;
            if (name == "__DATA_CONST")
                return 1;
            if (name == "__DATA")
                return 2;
            if (name == "__LINKEDIT")
                return 4;
            return 3;
        };

        std::string_view x = a->cmd.get_segname();
        std::string_view y = b->cmd.get_segname();
        return std::tuple{get_rank(x), x} < std::tuple{get_rank(y), y};
    }

    template<typename E>
    static bool compare_chunks(const Chunk<E> *a, const Chunk<E> *b) {
        assert(a->hdr.get_segname() == b->hdr.get_segname());

        auto is_bss = [](const Chunk<E> *x) {
            return x->hdr.type == S_ZEROFILL || x->hdr.type == S_THREAD_LOCAL_ZEROFILL;
        };

        if (is_bss(a) != is_bss(b))
            return !is_bss(a);

        static const std::string_view rank[] = {
            // __TEXT
            "__mach_header",
            "__stubs",
            "__text",
            "__stub_helper",
            "__gcc_except_tab",
            "__cstring",
            "__eh_frame",
            "__unwind_info",
            // __DATA_CONST
            "__got",
            "__const",
            // __DATA
            "__mod_init_func",
            "__la_symbol_ptr",
            "__thread_ptrs",
            "__data",
            "__objc_imageinfo",
            "__thread_vars",
            "__thread_ptr",
            "__thread_data",
            "__thread_bss",
            "__common",
            "__bss",
            // __LINKEDIT
            "__rebase",
            "__binding",
            "__weak_binding",
            "__lazy_binding",
            "__chainfixups",
            "__export",
            "__func_starts",
            "__data_in_code",
            "__symbol_table",
            "__ind_sym_tab",
            "__string_table",
            "__code_signature",
        };

        auto get_rank = [](std::string_view name) {
            i64 i = 0;
            for (; i < sizeof(rank) / sizeof(rank[0]); i++)
                if (name == rank[i])
                    return i;
            return i;
        };

        std::string_view x = a->hdr.get_sectname();
        std::string_view y = b->hdr.get_sectname();
        return std::tuple{get_rank(x), x} < std::tuple{get_rank(y), y};
    }

    template<typename E>
    static void create_synthetic_chunks(Context<E> &ctx) {
        Timer t(ctx, "create_synthetic_chunks");

        ctx.chained_fixups.reset(new ChainedFixupsSection<E>(ctx));

        // Create a __DATA,__objc_imageinfo section.
        ctx.image_info = ObjcImageInfoSection<E>::create(ctx);

        // Create a __LINKEDIT,__func_starts section.
        if (ctx.arg.function_starts)
            ctx.function_starts.reset(new FunctionStartsSection(ctx));

        // Create a __LINKEDIT,__data_in_code section.
        if (ctx.arg.data_in_code_info)
            ctx.data_in_code.reset(new DataInCodeSection(ctx));

        // Create a __TEXT,__init_offsets section.
        if (ctx.arg.init_offsets)
            ctx.init_offsets.reset(new InitOffsetsSection(ctx));

        // Add remaining subsections to output sections.
        for (ObjectFile<E> *file: ctx.objs)
            for (Subsection<E> *subsec: file->subsections)
                if (!subsec->added_to_osec)
                    subsec->isec->osec.add_subsec(subsec);

        // Add output sections to segments.
        for (Chunk<E> *chunk: ctx.chunks) {
            if (chunk != ctx.data)
                if (OutputSection<E> *osec = chunk->to_osec())
                    if (osec->members.empty())
                        continue;

            chunk->seg->chunks.push_back(chunk);
        }

        // Even though redundant, section headers have its containing segments name
        // in Mach-O.
        for (std::unique_ptr<OutputSegment<E> > &seg: ctx.segments)
            for (Chunk<E> *chunk: seg->chunks)
                if (!chunk->is_hidden)
                    chunk->hdr.set_segname(seg->cmd.segname);

        // Sort segments and output sections., 基于 compare_segments 函数进行比较
        sort(ctx.segments, compare_segments<E>);

        for (std::unique_ptr<OutputSegment<E> > &seg: ctx.segments)
            sort(seg->chunks, compare_chunks<E>);
    }

    // Merge S_CSTRING_LITERALS or S_{4,8,16}BYTE_LITERALS subsections by contents.
    template<typename E>
    static void uniquify_literals(Context<E> &ctx, OutputSection<E> &osec) {
        Timer t(ctx, "uniquify_literals " + std::string(osec.hdr.get_sectname()));

        struct Entry {
            Entry(Subsection<E> *subsec) : owner(subsec) {
            }

            Entry(const Entry &other) = default;

            Atomic<Subsection<E> *> owner = nullptr;
            Atomic<u8> p2align = 0;
        };

        struct SubsecRef {
            Subsection<E> *subsec = nullptr;
            u64 hash = 0;
            Entry *ent = nullptr;
        };

        std::vector<SubsecRef> vec(osec.members.size());

        // Estimate the number of unique strings.
        tbb::enumerable_thread_specific<HyperLogLog> estimators;

        tbb::parallel_for((i64) 0, (i64) osec.members.size(), [&](i64 i) {
            Subsection<E> *subsec = osec.members[i];
            u64 h = hash_string(subsec->get_contents());
            vec[i].subsec = subsec;
            vec[i].hash = h;
            estimators.local().insert(h);
        });

        HyperLogLog estimator;
        for (HyperLogLog &e: estimators)
            estimator.merge(e);

        // Create a hash map large enough to hold all strings.
        ConcurrentMap<Entry> map(estimator.get_cardinality() * 3 / 2);

        // Insert all strings into the hash table.
        tbb::parallel_for_each(vec, [&](SubsecRef &ref) {
            if (!ref.subsec)
                return;

            std::string_view key = ref.subsec->get_contents();
            ref.ent = map.insert(key, ref.hash, {ref.subsec}).first;

            Subsection<E> *existing = ref.ent->owner;
            while (existing->isec->file.priority < ref.subsec->isec->file.priority &&
                   !ref.ent->owner.compare_exchange_weak(existing, ref.subsec,
                                                         std::memory_order_relaxed));

            update_maximum(ref.ent->p2align, ref.subsec->p2align.load());
        });

        // Decide who will become the owner for each subsection.
        tbb::parallel_for_each(vec, [&](SubsecRef &ref) {
            if (ref.subsec && ref.subsec != ref.ent->owner) {
                ref.subsec->replacer = ref.ent->owner;
                ref.subsec->is_replaced = true;
            }
        });

        static Counter counter("num_merged_strings");
        counter += std::erase_if(osec.members, [](Subsection<E> *subsec) {
            return subsec->is_replaced;
        });
    }

    template<typename E>
    static void merge_mergeable_sections(Context<E> &ctx) {
        for (Chunk<E> *chunk: ctx.chunks) {
            if (OutputSection<E> *osec = chunk->to_osec()) {
                switch (chunk->hdr.type) {
                    case S_CSTRING_LITERALS:
                    case S_4BYTE_LITERALS:
                    case S_8BYTE_LITERALS:
                    case S_16BYTE_LITERALS:
                        uniquify_literals(ctx, *osec);
                        break;
                }
            }
        }

        // Rewrite relocations and symbols.
        tbb::parallel_for_each(ctx.objs, [&](ObjectFile<E> *file) {
            for (std::unique_ptr<InputSection<E> > &isec: file->sections)
                if (isec)
                    for (Relocation<E> &r: isec->rels)
                        if (r.subsec() && r.subsec()->is_replaced)
                            r.target = r.subsec()->replacer;
        });

        std::vector<InputFile<E> *> files;
        append(files, ctx.objs);
        append(files, ctx.dylibs);

        // Remove deduplicated subsections from each file's subsection vector.
        tbb::parallel_for_each(ctx.objs, [&](ObjectFile<E> *file) {
            std::erase_if(file->subsections, [](Subsection<E> *subsec) {
                return subsec->is_replaced;
            });
        });
    }

    template<typename E>
    static void scan_relocations(Context<E> &ctx) {
        tbb::parallel_for_each(ctx.objs, [&](ObjectFile<E> *file) {
            for (Subsection<E> *subsec: file->subsections) {
                subsec->scan_relocations(ctx);
            }
        });

        std::vector<InputFile<E> *> files;
        append(files, ctx.objs);
        append(files, ctx.dylibs);

        std::vector<std::vector<Symbol<E> *> > vec(files.size());

        tbb::parallel_for((i64) 0, (i64) files.size(), [&](i64 i) {
            for (Symbol<E> *sym: files[i]->syms)
                if (sym && sym->file == files[i] && sym->flags)
                    vec[i].push_back(sym);
        });

        for (std::span<Symbol<E> *> syms: vec) {
            for (Symbol<E> *sym: syms) {
                if (sym->flags & NEEDS_GOT)
                    ctx.got.add(ctx, sym);

                if (sym->flags & NEEDS_STUB) {
                    if (ctx.arg.bind_at_load || ctx.arg.fixup_chains)
                        ctx.got.add(ctx, sym);
                    ctx.stubs.add(ctx, sym);
                }

                sym->flags = 0;
            }
        }
    }

    template<typename E>
    static i64 assign_offsets(Context<E> &ctx) {
        Timer t(ctx, "assign_offsets");

        i64 sect_idx = 1;
        for (std::unique_ptr<OutputSegment<E> > &seg: ctx.segments)
            for (Chunk<E> *chunk: seg->chunks)
                if (!chunk->is_hidden)
                    chunk->sect_idx = sect_idx++;

        i64 fileoff = 0;
        i64 vmaddr = ctx.arg.pagezero_size;

        for (std::unique_ptr<OutputSegment<E> > &seg: ctx.segments) {
            seg->set_offset(ctx, fileoff, vmaddr);
            fileoff += seg->cmd.filesize;
            vmaddr += seg->cmd.vmsize;
        }
        return fileoff;
    }

    // An address of a symbol of type S_THREAD_LOCAL_VARIABLES is computed
    // as a relative address to the beginning of the first thread-local
    // section. This function finds the beginning address.
    template<typename E>
    static u64 get_tls_begin(Context<E> &ctx) {
        for (std::unique_ptr<OutputSegment<E> > &seg: ctx.segments)
            for (Chunk<E> *chunk: seg->chunks)
                if (chunk->hdr.type == S_THREAD_LOCAL_REGULAR ||
                    chunk->hdr.type == S_THREAD_LOCAL_ZEROFILL)
                    return chunk->hdr.addr;
        return 0;
    }

    template<typename E>
    static void copy_sections_to_output_file(Context<E> &ctx) {
        Timer t(ctx, "copy_sections_to_output_file");

        tbb::parallel_for_each(ctx.segments, [&](std::unique_ptr<OutputSegment<E> > &seg) {
            // Fill text segment paddings with single-byte NOP instructions so
            // that otool wouldn't out-of-sync when disassembling an output file.
            // Do this only for x86-64 because ARM64 instructions are always 4
            // bytes long.
            if constexpr (is_x86<E>)
                if (seg->cmd.get_segname() == "__TEXT")
                    memset(ctx.buf + seg->cmd.fileoff, 0x90, seg->cmd.filesize);

            tbb::parallel_for_each(seg->chunks, [&](Chunk<E> *sec) {
                if (sec->hdr.type != S_ZEROFILL) {
                    sec->copy_buf(ctx);
                }
            });
        });
    }

    template<typename E>
    static void compute_uuid(Context<E> &ctx) {
        Timer t(ctx, "copy_sections_to_output_file");

        // Compute a markle tree of height two.
        i64 filesize = ctx.output_file->filesize;
        i64 shard_size = 4096 * 1024;
        i64 num_shards = align_to(filesize, shard_size) / shard_size;
        std::vector<u8> shards(num_shards * SHA256_SIZE);

        tbb::parallel_for((i64) 0, num_shards, [&](i64 i) {
            u8 *begin = ctx.buf + shard_size * i;
            u8 *end = (i == num_shards - 1) ? ctx.buf + filesize : begin + shard_size;
            sha256_hash(begin, end - begin, shards.data() + i * SHA256_SIZE);
        });

        u8 buf[SHA256_SIZE];
        sha256_hash(shards.data(), shards.size(), buf);
        memcpy(ctx.uuid, buf, 16);
        ctx.mach_hdr.copy_buf(ctx);
    }

    template<typename E>
    static MappedFile<Context<E> > *
    strip_universal_header(Context<E> &ctx, MappedFile<Context<E> > *mf) {
        FatHeader &hdr = *(FatHeader *) mf->data;
        assert(hdr.magic == FAT_MAGIC);

        FatArch *arch = (FatArch *) (mf->data + sizeof(hdr));
        for (i64 i = 0; i < hdr.nfat_arch; i++)
            if (arch[i].cputype == E::cputype)
                return mf->slice(ctx, mf->name, arch[i].offset, arch[i].size);
        Fatal(ctx) << mf->name << ": fat file contains no matching file";
    }

    template<typename E>
    static void read_file(Context<E> &ctx, MappedFile<Context<E> > *mf) {
        if (get_file_type(ctx, mf) == FileType::MACH_UNIVERSAL)
            mf = strip_universal_header(ctx, mf);

        switch (get_file_type(ctx, mf)) {
            case FileType::TAPI:
            case FileType::MACH_DYLIB:
            case FileType::MACH_EXE: {
                DylibFile<E> *file = DylibFile<E>::create(ctx, mf);
                ctx.tg.run([file, &ctx] { file->parse(ctx); });
                ctx.dylibs.push_back(file);
                break;
            }
            case FileType::MACH_OBJ:
            case FileType::LLVM_BITCODE: {
                ObjectFile<E> *file = ObjectFile<E>::create(ctx, mf, "");
                ctx.tg.run([file, &ctx] { file->parse(ctx); });
                ctx.objs.push_back(file);
                break;
            }
            case FileType::AR:
                for (MappedFile<Context<E> > *child: read_archive_members(ctx, mf)) {
                    if (get_file_type(ctx, child) == FileType::MACH_OBJ) {
                        ObjectFile<E> *file = ObjectFile<E>::create(ctx, child, mf->name);
                        ctx.tg.run([file, &ctx] { file->parse(ctx); });
                        ctx.objs.push_back(file);
                    }
                }
                break;
            default:
                Fatal(ctx) << mf->name << ": unknown file type";
                break;
        }
    }

    // template<typename E>
    // static bool has_dylib(Context<E> &ctx, std::string_view path) {
    //     for (DylibFile<E> *file: ctx.dylibs)
    //         if (file->install_name == path)
    //             return true;
    //     return false;
    // }

    template<typename E>
    static void read_input_files(Context<E> &ctx, std::span<std::string> args) {
        std::unordered_set<std::string> libs;

        // 闭包函数 search, [&] 表示作用域捕获，& 会捕获闭包外部作用域中的所有变量, 接收一个参数 names 返回 MappedFile 结构
        auto search = [&](std::vector<std::string> names) -> MappedFile<Context<E> > *{
            for (std::string dir: ctx.arg.library_paths) {
                for (std::string name: names) {
                    std::string path = dir + "/lib" + name;
                    if (MappedFile<Context<E> > *mf = MappedFile<Context<E> >::open(ctx, path))
                        return mf;
                    ctx.missing_files.insert(path);
                }
            }
            return nullptr;
        };

        auto read_library = [&](std::string name) {
            if (!libs.insert(name).second)
                return;

            MappedFile<Context<E> > *mf = search({name + ".tbd", name + ".dylib", name + ".a"});
            if (!mf)
                Fatal(ctx) << "library not found: -l" << name;
            read_file(ctx, mf);
        };

        while (!args.empty()) {
            const std::string &opt = args[0];
            args = args.subspan(1);

            if (!opt.starts_with('-')) {
                read_file(ctx, MappedFile<Context<E> >::must_open(ctx, opt));
                continue;
            }

            if (args.empty())
                Fatal(ctx) << opt << ": missing argument";

            const std::string &arg = args[0];
            args = args.subspan(1);
            ReaderContext orig = ctx.reader;

            if (opt == "-l") {
                read_library(arg);
            } else {
                unreachable();
            }

            ctx.reader = orig;
        }

        // 阻塞 tg 等待所有的并行任务完成
        ctx.tg.wait();

        std::unordered_set<std::string_view> hoisted_libs;
        for (i64 i = 0; i < ctx.dylibs.size(); i++) // "/usr/lib/libSystem.B.dylib"
            for (DylibFile<E> *file: ctx.dylibs[i]->hoisted_libs)
                if (hoisted_libs.insert(file->install_name).second)
                    ctx.dylibs.push_back(file);

        if (ctx.objs.empty())
            Fatal(ctx) << "no input files";

        for (ObjectFile<E> *file: ctx.objs)
            file->priority = ctx.file_priority++;
        for (DylibFile<E> *dylib: ctx.dylibs)
            dylib->priority = ctx.file_priority++;

        for (i64 i = 1; DylibFile<E> *file: ctx.dylibs)
            if (file->dylib_idx != BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE)
                file->dylib_idx = i++;
    }

    template<typename E>
    int macho_main(int argc, char **argv) {
        Context<E> ctx;

        // 函数解析
        for (i64 i = 0; i < argc; i++)
            ctx.cmdline_args.push_back(argv[i]);

        std::vector<std::string> file_args = parse_nonpositional_args(ctx);

        tbb::global_control tbb_cont(tbb::global_control::max_allowed_parallelism,
                                     ctx.arg.thread_count);

        read_input_files(ctx, file_args);

        // `-ObjC` is an option to load all members of static archive
        // libraries that implement an Objective-C class or category.
        resolve_symbols(ctx);

        if (ctx.output_type == MH_EXECUTE && !ctx.arg.entry->file)
            Error(ctx) << "undefined entry point symbol: " << *ctx.arg.entry;

        create_internal_file(ctx);

        for (ObjectFile<E> *file: ctx.objs)
            file->convert_common_symbols(ctx);

        dead_strip(ctx);

        create_synthetic_chunks(ctx);

        merge_mergeable_sections(ctx);

        for (ObjectFile<E> *file: ctx.objs) {
            file->check_duplicate_symbols(ctx);
        }

        bool has_pagezero_seg = ctx.arg.pagezero_size;
        for (i64 i = 0; i < ctx.segments.size(); i++)
            ctx.segments[i]->seg_idx = (has_pagezero_seg ? i + 1 : i);

        compute_import_export(ctx);

        scan_relocations(ctx);

        i64 output_size = assign_offsets(ctx);
        ctx.tls_begin = get_tls_begin(ctx);

        ctx.output_file = OutputFile<Context<E> >::open(ctx, ctx.arg.output, output_size, 0777);
        ctx.buf = ctx.output_file->buf;

        copy_sections_to_output_file(ctx);

        ctx.chained_fixups->write_fixup_chains(ctx);

        compute_uuid(ctx);

        ctx.output_file->close(ctx);

        ctx.checkpoint();

        if (ctx.arg.quick_exit) {
            std::cout << std::flush;
            std::cerr << std::flush;
            _exit(0);
        }

        return 0;
    }

    using E = MOLD_TARGET;

#ifdef MOLD_ARM64

    extern template int macho_main<ARM64_32>(int, char **);

    extern template int macho_main<X86_64>(int, char **);

    int main(int argc, char **argv) {
        return macho_main<X86_64>(argc, argv);
    }

#else

template int macho_main<E>(int, char **);

#endif
}

// 文件 dead-strip.cc

#include "mold.h"

#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for_each.h>

namespace mold::macho {

template <typename E>
static void collect_root_set(Context<E> &ctx,
                             tbb::concurrent_vector<Subsection<E> *> &rootset) {
  Timer t(ctx, "collect_root_set");

  auto add = [&](Symbol<E> *sym) {
    if (sym && sym->subsec)
      rootset.push_back(sym->subsec);
  };

  auto keep = [&](Symbol<E> *sym) {
    if (sym->no_dead_strip)
      return true;
    if (ctx.output_type == MH_DYLIB || ctx.output_type == MH_BUNDLE)
      if (sym->visibility == SCOPE_GLOBAL)
        return true;
    return false;
  };

  tbb::parallel_for_each(ctx.objs, [&](ObjectFile<E> *file) {
    for (Symbol<E> *sym : file->syms)
      if (sym->file == file && keep(sym))
        add(sym);

    for (Symbol<E> *sym : file->init_functions)
      add(sym);

    for (Subsection<E> *subsec : file->subsections)
      if (const MachSection<E> &hdr = subsec->isec->hdr;
          (hdr.attr & S_ATTR_NO_DEAD_STRIP) ||
          hdr.type == S_MOD_INIT_FUNC_POINTERS ||
          hdr.type == S_MOD_TERM_FUNC_POINTERS)
        rootset.push_back(subsec);

    for (std::unique_ptr<CieRecord<E>> &cie : file->cies)
      add(cie->personality);
  });

  for (std::string_view name : ctx.arg.u)
    if (Symbol<E> *sym = get_symbol(ctx, name); sym->file)
      add(sym);

  add(ctx.arg.entry);

  if (ctx.stub_helper)
    add(get_symbol(ctx, "dyld_stub_binder"));
}

template <typename E>
static void visit(Context<E> &ctx, Subsection<E> *subsec) {
  if (!subsec)
    return;

  if (subsec->is_alive.test_and_set())
    return;

  for (Relocation<E> &rel : subsec->get_rels()) {
    if (rel.sym())
      visit(ctx, rel.sym()->subsec);
    else
      visit(ctx, rel.subsec());
  }

  for (UnwindRecord<E> &rec : subsec->get_unwind_records()) {
    visit(ctx, rec.subsec);
    visit(ctx, rec.lsda);

    if (rec.personality)
      visit(ctx, rec.personality->subsec);

    if (rec.fde) {
      visit(ctx, rec.fde->subsec);
      visit(ctx, rec.fde->lsda);
    }
  }
}

template <typename E>
static bool refers_to_live_subsection(Subsection<E> &subsec) {
  for (Relocation<E> &rel : subsec.get_rels()) {
    if (rel.sym()) {
      if (rel.sym()->subsec && rel.sym()->subsec->is_alive)
        return true;
    } else {
      if (rel.subsec()->is_alive)
        return true;
    }
  }
  return false;
}

template <typename E>
static void mark(Context<E> &ctx,
                 tbb::concurrent_vector<Subsection<E> *> &rootset) {
  Timer t(ctx, "mark");

  tbb::parallel_for_each(rootset, [&](Subsection<E> *subsec) {
    visit(ctx, subsec);
  });

  Atomic<bool> repeat;
  do {
    repeat = false;
    tbb::parallel_for_each(ctx.objs, [&](ObjectFile<E> *file) {
      for (Subsection<E> *subsec : file->subsections) {
        if ((subsec->isec->hdr.attr & S_ATTR_LIVE_SUPPORT) &&
            !subsec->is_alive &&
            refers_to_live_subsection(*subsec)) {
          visit(ctx, subsec);
          repeat = true;
        }
      }
    });
  } while (repeat);
}

template <typename E>
static void sweep(Context<E> &ctx) {
  Timer t(ctx, "sweep");

  tbb::parallel_for_each(ctx.objs, [&](ObjectFile<E> *file) {
    for (Symbol<E> *&sym : file->syms)
      if (sym->file == file && sym->subsec && !sym->subsec->is_alive)
        sym = nullptr;
  });

  tbb::parallel_for_each(ctx.objs, [&](ObjectFile<E> *file) {
    std::erase_if(file->subsections, [](Subsection<E> *subsec) {
      return !subsec->is_alive;
    });
  });
}

template <typename E>
void dead_strip(Context<E> &ctx) {
  Timer t(ctx, "dead_strip");

  tbb::concurrent_vector<Subsection<E> *> rootset;
  collect_root_set(ctx, rootset);
  mark(ctx, rootset);
  sweep(ctx);
}

using E = MOLD_TARGET;

template void dead_strip(Context<E> &);

} // namespace mold::macho



// 文件 input-files.cc
#include "mold.h"
#include "../common/archive-file.h"

#include <regex>

namespace mold::macho {

template <typename E>
std::ostream &operator<<(std::ostream &out, const InputFile<E> &file) {
  if (file.archive_name.empty())
    out << path_clean(file.filename);
  else
    out << path_clean(file.archive_name) << "(" << path_clean(file.filename) + ")";
  return out;
}

template <typename E>
void InputFile<E>::clear_symbols() {
  for (Symbol<E> *sym : syms) {
    if (__atomic_load_n(&sym->file, __ATOMIC_ACQUIRE) == this) {
      sym->visibility = SCOPE_LOCAL;
      sym->is_imported = false;
      sym->is_exported = false;
      sym->is_common = false;
      sym->is_weak = false;
      sym->is_abs = false;
      sym->is_tlv = false;
      sym->no_dead_strip = false;
      sym->subsec = nullptr;
      sym->value = 0;
      __atomic_store_n(&sym->file, nullptr, __ATOMIC_RELEASE);
    }
  }
}

template <typename E>
ObjectFile<E> *
ObjectFile<E>::create(Context<E> &ctx, MappedFile<Context<E>> *mf,
                      std::string archive_name) {
  ObjectFile<E> *obj = new ObjectFile<E>(mf);
  obj->archive_name = archive_name;
  obj->is_alive = archive_name.empty() || ctx.reader.all_load;
  obj->is_hidden = ctx.reader.hidden;
  ctx.obj_pool.emplace_back(obj);
  return obj;
};

template <typename E>
void ObjectFile<E>::parse(Context<E> &ctx) {
  if (get_file_type(ctx, this->mf) == FileType::LLVM_BITCODE) {
    // Open an compiler IR file
    load_lto_plugin(ctx);

    // It looks like module_create_from_memory is not thread-safe,
    // so protect it with a lock.
    {
      static std::mutex mu;
      std::scoped_lock lock(mu);

      this->lto_module =
        ctx.lto.module_create_from_memory(this->mf->data, this->mf->size);
      if (!this->lto_module)
        Fatal(ctx) << *this << ": lto_module_create_from_memory failed";
    }

    // Read a symbol table
    parse_lto_symbols(ctx);
    return;
  }

  if (SymtabCommand *cmd = (SymtabCommand *)find_load_command(ctx, LC_SYMTAB))
    mach_syms = {(MachSym<E> *)(this->mf->data + cmd->symoff), cmd->nsyms};

  parse_sections(ctx);

  if (MachHeader &hdr = *(MachHeader *)this->mf->data;
      hdr.flags & MH_SUBSECTIONS_VIA_SYMBOLS) {
    split_subsections_via_symbols(ctx);
  } else {
    init_subsections(ctx);
  }

  split_cstring_literals(ctx);
  split_fixed_size_literals(ctx);
  split_literal_pointers(ctx);

  sort(subsections, [](Subsection<E> *a, Subsection<E> *b) {
    return a->input_addr < b->input_addr;
  });

  parse_symbols(ctx);

  for (std::unique_ptr<InputSection<E>> &isec : sections)
    if (isec)
      isec->parse_relocations(ctx);

  if (unwind_sec)
    parse_compact_unwind(ctx);

  if (eh_frame_sec)
    parse_eh_frame(ctx);

  associate_compact_unwind(ctx);

  if (mod_init_func)
    parse_mod_init_func(ctx);
}

template <typename E>
void ObjectFile<E>::parse_sections(Context<E> &ctx) {
  SegmentCommand<E> *cmd =
    (SegmentCommand<E> *)find_load_command(ctx, LC_SEGMENT_64);
  if (!cmd)
    return;

  MachSection<E> *mach_sec = (MachSection<E> *)((u8 *)cmd + sizeof(*cmd));
  sections.resize(cmd->nsects);

  for (i64 i = 0; i < cmd->nsects; i++) {
    MachSection<E> &msec = mach_sec[i];

    if (msec.match("__LD", "__compact_unwind")) {
      unwind_sec = &msec;
      continue;
    }


    if (msec.match("__TEXT", "__eh_frame")) {
      eh_frame_sec = &msec;
      continue;
    }

    if (msec.match("__DATA", "__objc_imageinfo") ||
        msec.match("__DATA_CONST", "__objc_imageinfo")) {
      if (msec.size != sizeof(ObjcImageInfo))
        Fatal(ctx) << *this << ": __objc_imageinfo: invalid size";

      objc_image_info =
        (ObjcImageInfo *)(this->mf->get_contents().data() + msec.offset);

      if (objc_image_info->version != 0)
        Fatal(ctx) << *this << ": __objc_imageinfo: unknown version: "
                   << (u32)objc_image_info->version;
      continue;
    }

    if (ctx.arg.init_offsets && msec.type == S_MOD_INIT_FUNC_POINTERS) {
      mod_init_func = &msec;
      continue;
    }

    if (msec.match("__DWARF", "__debug_info"))
      has_debug_info = true;

    if (msec.get_segname() == "__LLVM" || (msec.attr & S_ATTR_DEBUG))
      continue;

    sections[i].reset(new InputSection<E>(ctx, *this, msec, i));
  }
}

template <typename E>
static bool always_split(InputSection<E> &isec) {
  if (isec.hdr.match("__TEXT", "__eh_frame"))
    return true;

  u32 ty = isec.hdr.type;
  return ty == S_4BYTE_LITERALS  || ty == S_8BYTE_LITERALS   ||
         ty == S_16BYTE_LITERALS || ty == S_LITERAL_POINTERS ||
         ty == S_CSTRING_LITERALS;
}

template <typename E>
void ObjectFile<E>::split_subsections_via_symbols(Context<E> &ctx) {
  struct MachSymOff {
    MachSym<E> *msym;
    i64 symidx;
  };

  std::vector<MachSymOff> msyms;

  // Find all symbols whose type is N_SECT.
  for (i64 i = 0; i < mach_syms.size(); i++)
    if (MachSym<E> &msym = mach_syms[i];
        !msym.stab && msym.type == N_SECT && sections[msym.sect - 1])
      msyms.push_back({&msym, i});

  // Sort by address
  sort(msyms, [](const MachSymOff &a, const MachSymOff &b) {
    return std::tuple(a.msym->sect, a.msym->value) <
           std::tuple(b.msym->sect, b.msym->value);
  });

  sym_to_subsec.resize(mach_syms.size());

  // Split each input section
  for (i64 i = 0; i < sections.size(); i++) {
    InputSection<E> *isec = sections[i].get();
    if (!isec || always_split(*isec))
      continue;

    // We start with one big subsection and split it as we process symbols
    auto add_subsec = [&](u32 addr) {
      Subsection<E> *subsec = new Subsection<E>{
        .isec = isec,
        .input_addr = addr,
        .input_size = (u32)(isec->hdr.addr + isec->hdr.size - addr),
        .p2align = (u8)isec->hdr.p2align,
        .is_alive = !ctx.arg.dead_strip,
      };
      subsec_pool.emplace_back(subsec);
      subsections.push_back(subsec);
    };

    add_subsec(isec->hdr.addr);

    // Find the symbols in the given section
    struct Less {
      bool operator()(MachSymOff &m, i64 idx) { return m.msym->sect < idx; }
      bool operator()(i64 idx, MachSymOff &m) { return idx < m.msym->sect; }
    };

    auto [it, end] = std::equal_range(msyms.begin(), msyms.end(), i + 1, Less{});

    for (; it != end; it++) {
      // Split the last subsection into two with a symbol without N_ALT_ENTRY
      // as a boundary. We don't want to create an empty subsection if there
      // are two symbols at the same address.
      MachSymOff &m = *it;

      if (!(m.msym->desc & N_ALT_ENTRY)) {
        Subsection<E> &last = *subsections.back();
        i64 size1 = (i64)m.msym->value - (i64)last.input_addr;
        i64 size2 = (i64)isec->hdr.addr + (i64)isec->hdr.size - (i64)m.msym->value;
        if (size1 > 0 && size2 > 0) {
          last.input_size = size1;
          add_subsec(m.msym->value);
        }
      }
      sym_to_subsec[m.symidx] = subsections.back();
    }
  }
}

// If a section is not splittable (i.e. doesn't have the
// MH_SUBSECTIONS_VIA_SYMBOLS bit), we create one subsection for it
// and let it cover the entire section.
template <typename E>
void ObjectFile<E>::init_subsections(Context<E> &ctx) {
  subsections.resize(sections.size());

  for (i64 i = 0; i < sections.size(); i++) {
    InputSection<E> *isec = sections[i].get();
    if (!isec || always_split(*isec))
      continue;

    Subsection<E> *subsec = new Subsection<E>{
      .isec = isec,
      .input_addr = (u32)isec->hdr.addr,
      .input_size = (u32)isec->hdr.size,
      .p2align = (u8)isec->hdr.p2align,
      .is_alive = !ctx.arg.dead_strip,
    };
    subsec_pool.emplace_back(subsec);
    subsections[i] = subsec;
  }

  sym_to_subsec.resize(mach_syms.size());

  for (i64 i = 0; i < mach_syms.size(); i++) {
    MachSym<E> &msym = mach_syms[i];
    if (!msym.stab && msym.type == N_SECT)
      sym_to_subsec[i] = subsections[msym.sect - 1];
  }

  std::erase(subsections, nullptr);
}

// Split __cstring section.
template <typename E>
void ObjectFile<E>::split_cstring_literals(Context<E> &ctx) {
  for (std::unique_ptr<InputSection<E>> &isec : sections) {
    if (!isec || isec->hdr.type != S_CSTRING_LITERALS)
      continue;

    std::string_view str = isec->contents;
    size_t pos = 0;

    while (pos < str.size()) {
      size_t end = str.find('\0', pos);
      if (end == str.npos)
        Fatal(ctx) << *this << " corruupted cstring section: " << *isec;

      end = str.find_first_not_of('\0', end);
      if (end == str.npos)
        end = str.size();

      // A constant string in __cstring has no alignment info, so we
      // need to infer it.
      Subsection<E> *subsec = new Subsection<E>{
        .isec = &*isec,
        .input_addr = (u32)(isec->hdr.addr + pos),
        .input_size = (u32)(end - pos),
        .p2align = std::min<u8>(isec->hdr.p2align, std::countr_zero(pos)),
        .is_alive = !ctx.arg.dead_strip,
      };

      subsec_pool.emplace_back(subsec);
      subsections.push_back(subsec);
      pos = end;
    }
  }
}

// Split S_{4,8,16}BYTE_LITERALS sections
template <typename E>
void ObjectFile<E>::split_fixed_size_literals(Context<E> &ctx) {
  auto split = [&](InputSection<E> &isec, u32 size) {
    if (isec.contents.size() % size)
      Fatal(ctx) << *this << ": invalid literals section";

    for (i64 pos = 0; pos < isec.contents.size(); pos += size) {
      Subsection<E> *subsec = new Subsection<E>{
        .isec = &isec,
        .input_addr = (u32)(isec.hdr.addr + pos),
        .input_size = size,
        .p2align = (u8)std::countr_zero(size),
        .is_alive = !ctx.arg.dead_strip,
      };

      subsec_pool.emplace_back(subsec);
      subsections.push_back(subsec);
    }
  };

  for (std::unique_ptr<InputSection<E>> &isec : sections) {
    if (!isec)
      continue;

    switch (isec->hdr.type) {
    case S_4BYTE_LITERALS:
      split(*isec, 4);
      break;
    case S_8BYTE_LITERALS:
      split(*isec, 8);
      break;
    case S_16BYTE_LITERALS:
      split(*isec, 16);
      break;
    }
  }
}

// Split S_LITERAL_POINTERS sections such as __DATA,__objc_selrefs.
template <typename E>
void ObjectFile<E>::split_literal_pointers(Context<E> &ctx) {
  for (std::unique_ptr<InputSection<E>> &isec : sections) {
    if (!isec || isec->hdr.type != S_LITERAL_POINTERS)
      continue;

    std::string_view str = isec->contents;
    assert(str.size() % sizeof(Word<E>) == 0);

    for (i64 pos = 0; pos < str.size(); pos += sizeof(Word<E>)) {
      Subsection<E> *subsec = new Subsection<E>{
        .isec = &*isec,
        .input_addr = (u32)(isec->hdr.addr + pos),
        .input_size = sizeof(Word<E>),
        .p2align = (u8)std::countr_zero(sizeof(Word<E>)),
        .is_alive = !ctx.arg.dead_strip,
      };

      subsec_pool.emplace_back(subsec);
      subsections.push_back(subsec);
    }
  }
}

template <typename E>
void ObjectFile<E>::parse_symbols(Context<E> &ctx) {
  SymtabCommand *cmd = (SymtabCommand *)find_load_command(ctx, LC_SYMTAB);
  this->syms.reserve(mach_syms.size());

  i64 nlocal = 0;
  for (MachSym<E> &msym : mach_syms)
    if (!msym.is_extern)
      nlocal++;
  local_syms.reserve(nlocal);

  for (i64 i = 0; i < mach_syms.size(); i++) {
    MachSym<E> &msym = mach_syms[i];
    std::string_view name = (char *)(this->mf->data + cmd->stroff + msym.stroff);

    // Global symbol
    if (msym.is_extern) {
      this->syms.push_back(get_symbol(ctx, name));
      continue;
    }

    // Local symbol
    local_syms.emplace_back(name);
    Symbol<E> &sym = local_syms.back();
    this->syms.push_back(&sym);

    sym.file = this;
    sym.visibility = SCOPE_LOCAL;
    sym.no_dead_strip = (msym.desc & N_NO_DEAD_STRIP);

    if (msym.type == N_ABS) {
      sym.value = msym.value;
      sym.is_abs = true;
    } else if (!msym.stab && msym.type == N_SECT) {
      sym.subsec = sym_to_subsec[i];
      if (!sym.subsec)
        sym.subsec = find_subsection(ctx, msym.value);

      // Subsec is null if a symbol is in a __compact_unwind.
      if (sym.subsec) {
        sym.value = msym.value - sym.subsec->input_addr;
        sym.is_tlv = (sym.subsec->isec->hdr.type == S_THREAD_LOCAL_VARIABLES);
      } else {
        sym.value = msym.value;
      }
    }
  }
}

// A Mach-O object file may contain command line option-like directives
// such as "-lfoo" in its LC_LINKER_OPTION command. This function returns
// such directives.
template <typename E>
std::vector<std::string> ObjectFile<E>::get_linker_options(Context<E> &ctx) {
  if (get_file_type(ctx, this->mf) == FileType::LLVM_BITCODE)
    return {};

  MachHeader &hdr = *(MachHeader *)this->mf->data;
  u8 *p = this->mf->data + sizeof(hdr);
  std::vector<std::string> vec;

  for (i64 i = 0; i < hdr.ncmds; i++) {
    LoadCommand &lc = *(LoadCommand *)p;
    p += lc.cmdsize;

    if (lc.cmd == LC_LINKER_OPTION) {
      LinkerOptionCommand *cmd = (LinkerOptionCommand *)&lc;
      char *buf = (char *)cmd + sizeof(*cmd);
      for (i64 i = 0; i < cmd->count; i++) {
        vec.push_back(buf);
        buf += vec.back().size() + 1;
      }
    }
  }
  return vec;
}

template <typename E>
LoadCommand *ObjectFile<E>::find_load_command(Context<E> &ctx, u32 type) {
  if (!this->mf)
    return nullptr;

  MachHeader &hdr = *(MachHeader *)this->mf->data;
  u8 *p = this->mf->data + sizeof(hdr);

  for (i64 i = 0; i < hdr.ncmds; i++) {
    LoadCommand &lc = *(LoadCommand *)p;
    if (lc.cmd == type)
      return &lc;
    p += lc.cmdsize;
  }
  return nullptr;
}

template <typename E>
Subsection<E> *
ObjectFile<E>::find_subsection(Context<E> &ctx, u32 addr) {
  assert(subsections.size() > 0);

  auto it = std::partition_point(subsections.begin(), subsections.end(),
                                 [&](Subsection<E> *subsec) {
    return subsec->input_addr <= addr;
  });

  if (it == subsections.begin())
    return nullptr;
  return *(it - 1);
}

// __compact_unwind consitss of fixed-sized records so-called compact
// unwinding records. There is usually a compact unwinding record for each
// function, and the record explains how to handle exceptions for that
// function.
//
// Output file's __compact_unwind contains not only unwinding records but
// also contains a two-level lookup table to quickly find out an unwinding
// record for a given function address. When an exception is thrown at
// runtime, the runtime looks up the table with the current program
// counter as a key to find out an unwinding record to know how to handle
// the exception.
//
// In order to construct the lookup table, we need to parse input files'
// unwinding records. The following function does that.
template <typename E>
void ObjectFile<E>::parse_compact_unwind(Context<E> &ctx) {
  MachSection<E> &hdr = *unwind_sec;

  if (hdr.size % sizeof(CompactUnwindEntry<E>))
    Fatal(ctx) << *this << ": invalid __compact_unwind section size";

  i64 num_entries = hdr.size / sizeof(CompactUnwindEntry<E>);
  unwind_records.reserve(num_entries);

  CompactUnwindEntry<E> *src =
    (CompactUnwindEntry<E> *)(this->mf->data + hdr.offset);

  // Read compact unwind entries
  for (i64 i = 0; i < num_entries; i++) {
    unwind_records.push_back(UnwindRecord<E>{
      .code_len = src[i].code_len,
      .encoding = src[i].encoding,
    });
  }

  auto find_symbol = [&](u32 addr) -> Symbol<E> * {
    for (i64 i = 0; i < mach_syms.size(); i++)
      if (MachSym<E> &msym = mach_syms[i]; msym.is_extern && msym.value == addr)
        return this->syms[i];
    return nullptr;
  };

  // Read relocations
  MachRel *mach_rels = (MachRel *)(this->mf->data + hdr.reloff);
  for (i64 i = 0; i < hdr.nreloc; i++) {
    MachRel &r = mach_rels[i];
    if (r.offset >= hdr.size)
      Fatal(ctx) << *this << ": relocation offset too large: " << i;

    i64 idx = r.offset / sizeof(CompactUnwindEntry<E>);
    UnwindRecord<E> &dst = unwind_records[idx];

    auto error = [&] {
      Fatal(ctx) << *this << ": __compact_unwind: unsupported relocation: " << i
                 << " " << *this->syms[r.idx];
    };

    if ((1 << r.p2size) != sizeof(Word<E>) || r.type != E::abs_rel)
      error();

    switch (r.offset % sizeof(CompactUnwindEntry<E>)) {
    case offsetof(CompactUnwindEntry<E>, code_start): {
      Subsection<E> *target;
      if (r.is_extern) {
        dst.subsec = sym_to_subsec[r.idx];
        dst.input_offset = src[idx].code_start;
      } else {
        dst.subsec = find_subsection(ctx, src[idx].code_start);
        dst.input_offset = dst.subsec->input_addr - src[idx].code_start;
      }

      if (!dst.subsec)
        error();
      break;
    }
    case offsetof(CompactUnwindEntry<E>, personality):
      if (r.is_extern) {
        dst.personality = this->syms[r.idx];
      } else {
        u32 addr = *(ul32 *)(this->mf->data + hdr.offset + r.offset);
        dst.personality = find_symbol(addr);
      }

      if (!dst.personality)
        Fatal(ctx) << *this << ": __compact_unwind: unsupported "
                   << "personality reference: " << i;
      break;
    case offsetof(CompactUnwindEntry<E>, lsda): {
      u32 addr = *(ul32 *)((u8 *)this->mf->data + hdr.offset + r.offset);

      if (r.is_extern) {
        dst.lsda = sym_to_subsec[r.idx];
        dst.lsda_offset = addr;
      } else {
        dst.lsda = find_subsection(ctx, addr);
        if (!dst.lsda)
          error();
        dst.lsda_offset = addr - dst.lsda->input_addr;
      }
      break;
    }
    default:
      error();
    }
  }

  // We want to ignore compact unwind records that point to DWARF unwind
  // info because we synthesize them ourselves. Object files usually don't
  // contain such records, but `ld -r` often produces them.
  std::erase_if(unwind_records, [](UnwindRecord<E> &rec) {
    return (rec.encoding & UNWIND_MODE_MASK) == E::unwind_mode_dwarf;
  });

  for (UnwindRecord<E> &rec : unwind_records) {
    if (!rec.subsec)
      Fatal(ctx) << *this << ": __compact_unwind: missing relocation at offset 0x"
                 << std::hex << rec.input_offset;
    rec.subsec->has_compact_unwind = true;
  }
}

// __eh_frame contains variable-sized records called CIE and FDE.
// In an __eh_frame section, there's usually one CIE record followed
// by as many FDE records as the number of functions defined by the
// same input file.
//
// A CIE usually contains one PC-relative GOT-referencing relocation.
// FDE usually contains no relocations. However, object files created
// by `ld -r` contains many relocations for __eh_frame.
//
// This function applies relocations against __eh_frame input sections
// so that all __eh_frame contains only one relocation for an CIE and
// no relocation for FDEs.
template <typename E>
static void apply_eh_frame_relocs(Context<E> &ctx, ObjectFile<E> &file) {
  MachSection<E> &msec = *file.eh_frame_sec;
  u8 *buf = (u8 *)file.mf->get_contents().data() + msec.offset;
  MachRel *mach_rels = (MachRel *)(file.mf->data + msec.reloff);

  for (i64 i = 0; i < msec.nreloc; i++) {
    MachRel &r1 = mach_rels[i];

    switch (r1.type) {
    case E::subtractor_rel: {
      if (i + 1 == msec.nreloc)
        Fatal(ctx) << file << ": __eh_frame: invalid subtractor reloc";

      MachRel &r2 = mach_rels[++i];
      if (r2.type != E::abs_rel)
        Fatal(ctx) << file << ": __eh_frame: invalid subtractor reloc pair";

      u32 target1 = r1.is_extern ? file.mach_syms[r1.idx].value : r1.idx;
      u32 target2 = r2.is_extern ? file.mach_syms[r2.idx].value : r2.idx;

      if (r1.p2size == 2)
        *(ul32 *)(buf + r1.offset) += target2 - target1;
      else if (r1.p2size == 3)
        *(ul64 *)(buf + r1.offset) += (i32)(target2 - target1);
      else
        Fatal(ctx) << file << ": __eh_frame: invalid p2size";
      break;
    }
    case E::gotpc_rel:
      break;
    default:
      Fatal(ctx) << file << ": unknown relocation type";
    }
  }
}

template <typename E>
void ObjectFile<E>::parse_eh_frame(Context<E> &ctx) {
  apply_eh_frame_relocs(ctx, *this);

  const char *start = this->mf->get_contents().data() + eh_frame_sec->offset;
  std::string_view data(start, eh_frame_sec->size);

  // Split section contents into CIE and FDE records
  while (!data.empty()) {
    u32 len = *(ul32 *)data.data();
    if (len == 0xffff'ffff)
      Fatal(ctx) << *this
                 << ": __eh_frame record with an extended length is not supported";

    u32 offset = data.data() - start;

    u32 id = *(ul32 *)(data.data() + 4);
    if (id == 0) {
      cies.emplace_back(new CieRecord<E>{
        .file = this,
        .input_addr = (u32)(eh_frame_sec->addr + offset),
      });
    } else {
      u64 addr = *(il64 *)(data.data() + 8) + eh_frame_sec->addr + offset + 8;
      Subsection<E> *subsec = find_subsection(ctx, addr);
      if (!subsec)
        Fatal(ctx) << *this << ": __unwind_info: FDE with invalid function"
                   << " reference at 0x" << std::hex << offset;

      if (!subsec->has_compact_unwind) {
        fdes.push_back(FdeRecord<E>{
          .subsec = subsec,
          .input_addr = (u32)(eh_frame_sec->addr + offset),
       });
      }
    }

    data = data.substr(len + 4);
  }

  sort(fdes, [](const FdeRecord<E> &a, const FdeRecord<E> &b) {
    return a.subsec->input_addr < b.subsec->input_addr;
  });

  for (std::unique_ptr<CieRecord<E>> &cie : cies)
    cie->parse(ctx);
  for (FdeRecord<E> &fde : fdes)
    fde.parse(ctx);

  MachRel *mach_rels = (MachRel *)(this->mf->data + eh_frame_sec->reloff);

  auto find_cie = [&](u32 input_addr) -> CieRecord<E> * {
    for (std::unique_ptr<CieRecord<E>> &cie : cies)
      if (cie->input_addr <= input_addr &&
          input_addr < cie->input_addr + cie->size())
        return &*cie;
    Fatal(ctx) << *this << ": __eh_frame: unexpected relocation offset";
  };

  for (i64 i = 0; i < eh_frame_sec->nreloc; i++) {
    MachRel &r = mach_rels[i];
    if (r.type != E::gotpc_rel)
      continue;

    if (r.p2size != 2)
      Fatal(ctx) << *this << ": __eh_frame: unexpected p2size";
    if (!r.is_extern)
      Fatal(ctx) << *this << ": __eh_frame: unexpected is_extern value";

    CieRecord<E> *cie = find_cie(eh_frame_sec->addr + r.offset);
    cie->personality = this->syms[r.idx];
    cie->personality_offset = eh_frame_sec->addr + r.offset - cie->input_addr;
  }
}

template <typename E>
void ObjectFile<E>::associate_compact_unwind(Context<E> &ctx) {
  // If a subsection has a DWARF unwind info, we need to create a compact
  // unwind record that points to it.
  for (FdeRecord<E> &fde : fdes) {
    unwind_records.push_back(UnwindRecord<E>{
      .subsec = fde.subsec,
      .fde = &fde,
      .input_offset = 0,
      .code_len = fde.subsec->input_size,
    });
  }

  // Sort unwind entries by offset
  sort(unwind_records, [](const UnwindRecord<E> &a, const UnwindRecord<E> &b) {
    return std::tuple(a.subsec->input_addr, a.input_offset) <
           std::tuple(b.subsec->input_addr, b.input_offset);
  });

  // Associate unwind entries to subsections
  for (i64 i = 0, end = unwind_records.size(); i < end;) {
    Subsection<E> &subsec = *unwind_records[i].subsec;
    subsec.unwind_offset = i;

    i64 j = i + 1;
    while (j < end && unwind_records[j].subsec == &subsec)
      j++;
    subsec.nunwind = j - i;
    i = j;
  }
}

// __mod_init_func section contains pointers to glolbal initializers, e.g.
// functions that have to run before main().
//
// We can just copy input __mod_init_func sections into an output
// __mod_init_func. In this case, the output consists of absolute
// addresses of functions, which needs base relocation for PIE.
//
// If -init_offset is given, we translate __mod_init_func to __init_offset,
// which contains 32-bit offsets from the image base to initializer functions.
// __init_offset and __mod_init_func are functionally the same, but the
// former doesn't need to be base-relocated and thus a bit more efficient.
template <typename E>
void ObjectFile<E>::parse_mod_init_func(Context<E> &ctx) {
  MachSection<E> &hdr = *mod_init_func;

  if (hdr.size % sizeof(Word<E>))
    Fatal(ctx) << *this << ": __mod_init_func: unexpected section size";

  MachRel *begin = (MachRel *)(this->mf->data + hdr.reloff);
  std::vector<MachRel> rels(begin, begin + hdr.nreloc);

  sort(rels, [](const MachRel &a, const MachRel &b) {
    return a.offset < b.offset;
  });

  for (i64 i = 0; i < rels.size(); i++) {
    MachRel r = rels[i];

    if (r.type != E::abs_rel)
      Fatal(ctx) << *this << ": __mod_init_func: unexpected relocation type";
    if (r.offset != i * sizeof(Word<E>))
      Fatal(ctx) << *this << ": __mod_init_func: unexpected relocation offset";
    if (!r.is_extern)
      Fatal(ctx) << *this << ": __mod_init_func: unexpected is_extern value";

    Symbol<E> *sym = this->syms[r.idx];

    if (sym->visibility != SCOPE_LOCAL)
      Fatal(ctx) << *this << ": __mod_init_func: non-local initializer function";

    init_functions.push_back(sym);
  }
}

// Symbols with higher priorities overwrites symbols with lower priorities.
// Here is the list of priorities, from the highest to the lowest.
//
//  1. Strong defined symbol
//  2. Weak defined symbol
//  3. Strong defined symbol in a DSO/archive
//  4. Weak Defined symbol in a DSO/archive
//  5. Common symbol
//  6. Common symbol in an archive
//  7. Unclaimed (nonexistent) symbol
//
// Ties are broken by file priority.
template <typename E>
static u64 get_rank(InputFile<E> *file, bool is_common, bool is_weak) {
  bool is_in_archive = !file->is_alive;

  auto get_sym_rank = [&] {
    if (is_common) {
      assert(!file->is_dylib);
      return is_in_archive ? 6 : 5;
    }

    if (file->is_dylib || is_in_archive)
      return is_weak ? 4 : 3;
    return is_weak ? 2 : 1;
  };

  return (get_sym_rank() << 24) + file->priority;
}

template <typename E>
static u64 get_rank(Symbol<E> &sym) {
  if (!sym.file)
    return 7 << 24;
  return get_rank(sym.file, sym.is_common, sym.is_weak);
}

template <typename E>
void ObjectFile<E>::resolve_symbols(Context<E> &ctx) {
  for (i64 i = 0; i < this->syms.size(); i++) {
    MachSym<E> &msym = mach_syms[i];

    if (!msym.is_extern || msym.is_undef())
      continue;

    // Global symbols in a discarded segment (i.e. __LLVM segment) are
    // silently ignored.
    if (msym.type == N_SECT && !sym_to_subsec[i])
      continue;

    Symbol<E> &sym = *this->syms[i];
    std::scoped_lock lock(sym.mu);
    bool is_weak = (msym.desc & N_WEAK_DEF);

    if (get_rank(this, msym.is_common(), is_weak) < get_rank(sym)) {
      sym.file = this;
      sym.visibility = SCOPE_MODULE;
      sym.is_weak = is_weak;
      sym.no_dead_strip = (msym.desc & N_NO_DEAD_STRIP);

      switch (msym.type) {
      case N_UNDF:
        assert(msym.is_common());
        sym.subsec = nullptr;
        sym.value = msym.value;
        sym.is_common = true;
        sym.is_abs = false;
        sym.is_tlv = false;
        break;
      case N_ABS:
        sym.subsec = nullptr;
        sym.value = msym.value;
        sym.is_common = false;
        sym.is_abs = true;
        sym.is_tlv = false;
        break;
      case N_SECT:
        sym.subsec = sym_to_subsec[i];
        sym.value = msym.value - sym.subsec->input_addr;
        sym.is_common = false;
        sym.is_abs = false;
        sym.is_tlv = (sym.subsec->isec->hdr.type == S_THREAD_LOCAL_VARIABLES);
        break;
      default:
        Fatal(ctx) << sym << ": unknown symbol type: " << (u64)msym.type;
      }
    }
  }
}

template <typename E>
bool ObjectFile<E>::is_objc_object(Context<E> &ctx) {
  for (std::unique_ptr<InputSection<E>> &isec : sections)
    if (isec)
      if (isec->hdr.match("__DATA", "__objc_catlist") ||
          (isec->hdr.get_segname() == "__TEXT" &&
           isec->hdr.get_sectname().starts_with("__swift")))
        return true;

  for (i64 i = 0; i < this->syms.size(); i++)
    if (!mach_syms[i].is_undef() && mach_syms[i].is_extern &&
        this->syms[i]->name.starts_with("_OBJC_CLASS_$_"))
      return true;

  return false;
}

template <typename E>
void
ObjectFile<E>::mark_live_objects(Context<E> &ctx,
                                 std::function<void(ObjectFile<E> *)> feeder) {
  assert(this->is_alive);

  auto is_module_local = [&](MachSym<E> &msym) {
    return this->is_hidden || msym.is_private_extern ||
           ((msym.desc & N_WEAK_REF) && (msym.desc & N_WEAK_DEF));
  };

  for (i64 i = 0; i < this->syms.size(); i++) {
    MachSym<E> &msym = mach_syms[i];
    if (!msym.is_extern)
      continue;

    Symbol<E> &sym = *this->syms[i];
    std::scoped_lock lock(sym.mu);

    // If at least one symbol defines it as an GLOBAL symbol, the result
    // is an GLOBAL symbol instead of MODULE, so that the symbol is exported.
    if (!msym.is_undef() && !is_module_local(msym))
      sym.visibility = SCOPE_GLOBAL;

    if (InputFile<E> *file = sym.file)
      if (msym.is_undef() || (msym.is_common() && !sym.is_common))
        if (!file->is_alive.test_and_set() && !file->is_dylib)
          feeder((ObjectFile<E> *)file);
  }

  for (Subsection<E> *subsec : subsections)
    for (UnwindRecord<E> &rec : subsec->get_unwind_records())
      if (Symbol<E> *sym = rec.personality)
        if (InputFile<E> *file = sym->file)
          if (!file->is_alive.test_and_set() && !file->is_dylib)
            feeder((ObjectFile<E> *)file);
}

// review weiwenhao
template <typename E>
void ObjectFile<E>::convert_common_symbols(Context<E> &ctx) {
  for (i64 i = 0; i < this->mach_syms.size(); i++) {
    Symbol<E> &sym = *this->syms[i];
    MachSym<E> &msym = mach_syms[i];

  // 这段代码在链接器中的作用是处理 "common" 符号。
  // Common 符号是一种特殊类型的符号，通常用于未初始化的全局变量。
  // 链接器需要将这些符号放置在一个特定的段中，并确保它们具有适当的对齐和大小。
    if (sym.file == this && sym.is_common) {
      InputSection<E> *isec = get_common_sec(ctx);
      Subsection<E> *subsec = new Subsection<E>{
        .isec = isec,
        .input_size = (u32)msym.value,
        .p2align = (u8)msym.common_p2align,
        .is_alive = !ctx.arg.dead_strip,
      };

      subsections.emplace_back(subsec);

      sym.is_weak = false;
      sym.no_dead_strip = (msym.desc & N_NO_DEAD_STRIP);
      sym.subsec = subsec;
      sym.value = 0;
      sym.is_common = false;
      sym.is_abs = false;
      sym.is_tlv = false;
    }
  }
}

// 检查是否存在重复的符号, mach_syms 是当前 object file 的符号表, 而 syms 是所有 object file 的符号表
template <typename E>
void ObjectFile<E>::check_duplicate_symbols(Context<E> &ctx) {
  for (i64 i = 0; i < this->mach_syms.size(); i++) {
    Symbol<E> *sym = this->syms[i];
    MachSym<E> &msym = mach_syms[i];
    if (sym && sym->file && sym->file != this && !msym.is_undef() &&
        !msym.is_common() && !(msym.desc & N_WEAK_DEF))
      Error(ctx) << "duplicate symbol: " << *this << ": " << *sym->file
                 << ": " << *sym;
  }
}

template <typename E>
InputSection<E> *ObjectFile<E>::get_common_sec(Context<E> &ctx) {
  if (!common_sec) {
    MachSection<E> *hdr = new MachSection<E>;
    common_hdr.reset(hdr);

    memset(hdr, 0, sizeof(*hdr));
    hdr->set_segname("__DATA");
    hdr->set_sectname("__common");
    hdr->type = S_ZEROFILL;

    common_sec = new InputSection<E>(ctx, *this, *hdr, sections.size());
    sections.emplace_back(common_sec);
  }
  return common_sec;
}

template <typename E>
void ObjectFile<E>::parse_lto_symbols(Context<E> &ctx) {
  i64 nsyms = ctx.lto.module_get_num_symbols(this->lto_module);
  this->syms.reserve(nsyms);
  this->mach_syms2.reserve(nsyms);

  for (i64 i = 0; i < nsyms; i++) {
    std::string_view name = ctx.lto.module_get_symbol_name(this->lto_module, i);
    this->syms.push_back(get_symbol(ctx, name));

    u32 attr = ctx.lto.module_get_symbol_attribute(this->lto_module, i);
    MachSym<E> msym = {};

    switch (attr & LTO_SYMBOL_DEFINITION_MASK) {
    case LTO_SYMBOL_DEFINITION_REGULAR:
    case LTO_SYMBOL_DEFINITION_TENTATIVE:
    case LTO_SYMBOL_DEFINITION_WEAK:
      msym.type = N_ABS;
      break;
    case LTO_SYMBOL_DEFINITION_UNDEFINED:
    case LTO_SYMBOL_DEFINITION_WEAKUNDEF:
      msym.type = N_UNDF;
      break;
    default:
      unreachable();
    }

    switch (attr & LTO_SYMBOL_SCOPE_MASK) {
    case 0:
    case LTO_SYMBOL_SCOPE_INTERNAL:
    case LTO_SYMBOL_SCOPE_HIDDEN:
      break;
    case LTO_SYMBOL_SCOPE_DEFAULT:
    case LTO_SYMBOL_SCOPE_PROTECTED:
    case LTO_SYMBOL_SCOPE_DEFAULT_CAN_BE_HIDDEN:
      msym.is_extern = true;
      break;
    default:
      unreachable();
    }

    mach_syms2.push_back(msym);
  }

  mach_syms = mach_syms2;
}

template <typename E>
std::string_view ObjectFile<E>::get_linker_optimization_hints(Context<E> &ctx) {
  LinkEditDataCommand *cmd =
    (LinkEditDataCommand *)find_load_command(ctx, LC_LINKER_OPTIMIZATION_HINT);

  if (cmd)
    return {(char *)this->mf->data + cmd->dataoff, cmd->datasize};
  return {};
}

// As a space optimization, Xcode 14 or later emits code to just call
// `_objc_msgSend$foo` to call `_objc_msgSend` function with a selector
// `foo`.
//
// It is now the linker's responsibility to synthesize code and data
// for undefined symbol of the form `_objc_msgSend$<method_name>`.
// To do so, we need to synthsize three subsections containing the following
// pieces of code/data:
//
//  1. `__objc_stubs`:    containing machine code to call `_objc_msgSend`
//  2. `__objc_methname`: containing a null-terminated method name string
//  3. `__objc_selrefs`:  containing a pointer to the method name string
template <typename E>
void ObjectFile<E>::add_msgsend_symbol(Context<E> &ctx, Symbol<E> &sym) {
  assert(this == ctx.internal_obj);

  std::string_view prefix = "_objc_msgSend$";
  assert(sym.name.starts_with(prefix));

  this->syms.push_back(&sym);
  sym.file = this;

  Subsection<E> *subsec = add_methname_string(ctx, sym.name.substr(prefix.size()));
  ctx.objc_stubs->methnames.push_back(subsec);
  ctx.objc_stubs->selrefs.push_back(add_selrefs(ctx, *subsec));
  ctx.objc_stubs->hdr.size += ObjcStubsSection<E>::ENTRY_SIZE;
}

template <typename E>
Subsection<E> *
ObjectFile<E>::add_methname_string(Context<E> &ctx, std::string_view contents) {
  assert(this == ctx.internal_obj);
  assert(contents[contents.size()] == '\0');

  u64 addr = 0;
  if (!sections.empty()) {
    const MachSection<E> &hdr = sections.back()->hdr;
    addr = hdr.addr + hdr.size;
  }

  // Create a dummy Mach-O section
  MachSection<E> *msec = new MachSection<E>;
  mach_sec_pool.emplace_back(msec);

  memset(msec, 0, sizeof(*msec));
  msec->set_segname("__TEXT");
  msec->set_sectname("__objc_methname");
  msec->addr = addr;
  msec->size = contents.size() + 1;
  msec->type = S_CSTRING_LITERALS;

  // Create a dummy InputSection
  InputSection<E> *isec = new InputSection<E>(ctx, *this, *msec, sections.size());
  sections.emplace_back(isec);
  isec->contents = contents;

  Subsection<E> *subsec = new Subsection<E>{
    .isec = isec,
    .input_addr = (u32)addr,
    .input_size = (u32)contents.size() + 1,
    .p2align = 0,
    .is_alive = !ctx.arg.dead_strip,
  };

  subsec_pool.emplace_back(subsec);
  subsections.push_back(subsec);
  return subsec;
}

template <typename E>
Subsection<E> *
ObjectFile<E>::add_selrefs(Context<E> &ctx, Subsection<E> &methname) {
  assert(this == ctx.internal_obj);

  // Create a dummy Mach-O section
  MachSection<E> *msec = new MachSection<E>;
  mach_sec_pool.emplace_back(msec);

  memset(msec, 0, sizeof(*msec));
  msec->set_segname("__DATA");
  msec->set_sectname("__objc_selrefs");
  msec->addr = sections.back()->hdr.addr + sections.back()->hdr.size,
  msec->size = sizeof(Word<E>);
  msec->type = S_LITERAL_POINTERS;
  msec->attr = S_ATTR_NO_DEAD_STRIP;

  // Create a dummy InputSection
  InputSection<E> *isec = new InputSection<E>(ctx, *this, *msec, sections.size());
  sections.emplace_back(isec);
  isec->contents = "\0\0\0\0\0\0\0\0"sv;

  // Create a dummy relocation
  isec->rels.push_back(Relocation<E>{
    .target = &methname,
    .offset = 0,
    .type = E::abs_rel,
    .size = (u8)sizeof(Word<E>),
    .is_sym = false,
  });

  // Create a dummy subsection
  Subsection<E> *subsec = new Subsection<E>{
    .isec = isec,
    .input_addr = (u32)msec->addr,
    .input_size = sizeof(Word<E>),
    .rel_offset = 0,
    .nrels = 1,
    .p2align = (u8)std::countr_zero(sizeof(Word<E>)),
    .is_alive = !ctx.arg.dead_strip,
  };

  subsec_pool.emplace_back(subsec);
  subsections.push_back(subsec);
  return subsec;
}

template <typename E>
void ObjectFile<E>::compute_symtab_size(Context<E> &ctx) {
  auto get_oso_name = [&]() -> std::string {
    if (!this->mf)
      return "<internal>";

    std::string name = path_clean(this->mf->name);
    if (!this->mf->parent) {
      if (name.starts_with('/'))
        return name;
      return ctx.cwd + "/" + name;
    }

    std::string parent = path_clean(this->mf->parent->name);
    if (parent.starts_with('/'))
      return parent + "(" + name + ")";
    return ctx.cwd + "/" + parent + "(" + name + ")";
  };

  // Debug symbols. Mach-O executables and dylibs generally don't directly
  // contain debug info records. Instead, they have only symbols that
  // specify function/global variable names and their addresses along with
  // pathnames to object files. The debugger reads the symbols on startup
  // and read debug info from object files.
  //
  // Debug symbols are called "stab" symbols.
  bool emit_debug_syms = has_debug_info && !ctx.arg.S;

  if (emit_debug_syms) {
    this->oso_name = get_oso_name();
    if (!ctx.arg.oso_prefix.empty() &&
        this->oso_name.starts_with(ctx.arg.oso_prefix))
      this->oso_name = this->oso_name.substr(ctx.arg.oso_prefix.size());

    this->strtab_size += this->oso_name.size() + 1;
    this->num_stabs = 3;
  }

  // Symbols copied from an input symtab to the output symtab
  for (Symbol<E> *sym : this->syms) {
    if (!sym || sym->file != this || (sym->subsec && !sym->subsec->is_alive))
      continue;

    // Symbols starting with l or L are compiler-generated private labels
    // that should be stripped from the symbol table.
    if (sym->name.starts_with('l') || sym->name.starts_with('L'))
      continue;

    if (ctx.arg.x && sym->visibility == SCOPE_LOCAL)
      continue;

    if (sym->is_imported)
      this->num_undefs++;
    else if (sym->visibility == SCOPE_GLOBAL)
      this->num_globals++;
    else
      this->num_locals++;

    if (emit_debug_syms && sym->subsec)
      this->num_stabs += sym->subsec->isec->hdr.is_text() ? 2 : 1;

    this->strtab_size += sym->name.size() + 1;
    sym->output_symtab_idx = -2;
  }
}

template <typename E>
void ObjectFile<E>::populate_symtab(Context<E> &ctx) {
  MachSym<E> *buf = (MachSym<E> *)(ctx.buf + ctx.symtab.hdr.offset);
  u8 *strtab = ctx.buf + ctx.strtab.hdr.offset;
  i64 stroff = this->strtab_offset;

  // Write to the string table
  std::vector<i32> pos(this->syms.size());

  for (i64 i = 0; i < this->syms.size(); i++) {
    Symbol<E> *sym = this->syms[i];
    if (sym && sym->file == this && sym->output_symtab_idx != -1) {
      pos[i] = stroff;
      stroff += write_string(strtab + stroff, sym->name);
    }
  }

  // Write debug symbols. A stab symbol of type N_SO with a nonempty name
  // marks a start of a new object file.
  //
  // The first N_SO symbol is intended to have a source filename (e.g.
  // path/too/foo.cc), but it looks like lldb doesn't actually use that
  // symbol name. Souce filename is in the debug record and thus naturally
  // lldb can read it, so it doesn't make much sense to parse a debug
  // record just to set a source filename which will be ignored. So, we
  // always set a dummy name "-" as a filename.
  //
  // The following N_OSO symbol specifies a object file path, which is
  // followed by N_FUN, N_STSYM or N_GSYM symbols for functions,
  // file-scope global variables and global variables, respectively.
  //
  // N_FUN symbol is always emitted as a pair. The first N_FUN symbol
  // specifies the start address of a function, and the second specifies
  // the size.
  //
  // At the end of stab symbols, we have a N_SO symbol without symbol name
  // as an end marker.
  if (has_debug_info && !ctx.arg.S) {
    MachSym<E> *stab = buf + this->stabs_offset;
    i64 stab_idx = 2;

    stab[0].stroff = 2; // string "-"
    stab[0].n_type = N_SO;

    stab[1].stroff = stroff;
    stab[1].n_type = N_OSO;
    stab[1].sect = E::cpusubtype;
    stab[1].desc = 1;
    stroff += write_string(strtab + stroff, this->oso_name);

    for (i64 i = 0; i < this->syms.size(); i++) {
      Symbol<E> *sym = this->syms[i];
      if (!sym || sym->file != this || sym->output_symtab_idx == -1 || !sym->subsec)
        continue;

      stab[stab_idx].stroff = pos[i];
      stab[stab_idx].sect = sym->subsec->isec->osec.sect_idx;
      stab[stab_idx].value = sym->get_addr(ctx);

      if (sym->subsec->isec->hdr.is_text()) {
        stab[stab_idx].n_type = N_FUN;
        stab[stab_idx + 1].stroff = 1; // empty string
        stab[stab_idx + 1].n_type = N_FUN;
        stab[stab_idx + 1].value = sym->subsec->input_size;
        stab_idx += 2;
      } else {
        stab[stab_idx].n_type = (sym->visibility == SCOPE_LOCAL) ? N_STSYM : N_GSYM;
        stab_idx++;
      }
    }

    assert(stab_idx == this->num_stabs - 1);
    stab[stab_idx].stroff = 1; // empty string
    stab[stab_idx].n_type = N_SO;
    stab[stab_idx].sect = 1;
  }

  // Copy symbols from input symtabs to the output sytmab
  for (i64 i = 0; i < this->syms.size(); i++) {
    Symbol<E> *sym = this->syms[i];
    if (!sym || sym->file != this || sym->output_symtab_idx == -1)
      continue;

    MachSym<E> &msym = buf[sym->output_symtab_idx];
    msym.stroff = pos[i];
    msym.is_extern = (sym->visibility == SCOPE_GLOBAL);

    if (sym->subsec && sym->subsec->is_alive) {
      msym.type = N_SECT;
      msym.sect = sym->subsec->isec->osec.sect_idx;
      msym.value = sym->get_addr(ctx);
    } else if (sym == ctx.__dyld_private || sym == ctx.__mh_dylib_header ||
               sym == ctx.__mh_bundle_header || sym == ctx.___dso_handle) {
      msym.type = N_SECT;
      msym.sect = ctx.data->sect_idx;
      msym.value = 0;
    } else if (sym == ctx.__mh_execute_header) {
      msym.type = N_SECT;
      msym.sect = ctx.text->sect_idx;
      msym.value = 0;
    } else if (sym->is_imported) {
      msym.type = N_UNDF;
      msym.sect = N_UNDF;
    } else {
      msym.type = N_ABS;
      msym.sect = N_ABS;
    }
  }
}

static bool is_system_dylib(std::string_view path) {
  if (!path.starts_with("/usr/lib/") &&
      !path.starts_with("/System/Library/Frameworks/"))
    return false;

  static std::regex re(
    R"(/usr/lib/.+\.dylib|/System/Library/Frameworks/([^/]+)\.framework/.+/\1)",
    std::regex_constants::ECMAScript | std::regex_constants::optimize);

  return std::regex_match(path.begin(), path.end(), re);
}

template <typename E>
DylibFile<E>::DylibFile(Context<E> &ctx, MappedFile<Context<E>> *mf)
    : InputFile<E>(mf) {
  this->is_dylib = true;
  this->is_weak = ctx.reader.weak;
  this->is_reexported = ctx.reader.reexport;

  if (ctx.reader.implicit) {
    // Libraries implicitly specified by LC_LINKER_OPTION are dead-stripped
    // if not used.
    this->is_alive = false;
  } else {
    // Even if -dead_strip was not given, a dylib with
    // MH_DEAD_STRIPPABLE_DYLIB is dead-stripped if unreferenced.
    bool is_dead_strippable_dylib =
      get_file_type(ctx, mf) == FileType::MACH_DYLIB &&
      (((MachHeader *)mf->data)->flags & MH_DEAD_STRIPPABLE_DYLIB);

    bool is_dead_strippable = ctx.arg.dead_strip_dylibs || is_dead_strippable_dylib;
    this->is_alive = ctx.reader.needed || !is_dead_strippable;
  }
}

template <typename E>
DylibFile<E> *DylibFile<E>::create(Context<E> &ctx, MappedFile<Context<E>> *mf) {
  DylibFile<E> *file = new DylibFile<E>(ctx, mf);
  ctx.dylib_pool.emplace_back(file);
  return file;
}

template <typename E>
static MappedFile<Context<E>> *
find_external_lib(Context<E> &ctx, DylibFile<E> &loader, std::string path) {
  auto find = [&](std::string path) -> MappedFile<Context<E>> * {
    if (!path.starts_with('/'))
      return MappedFile<Context<E>>::open(ctx, path);

    for (const std::string &root : ctx.arg.syslibroot) {
      if (path.ends_with(".tbd")) {
        if (auto *file = MappedFile<Context<E>>::open(ctx, root + path))
          return file;
        continue;
      }

      if (path.ends_with(".dylib")) {
        std::string stem(path.substr(0, path.size() - 6));
        if (auto *file = MappedFile<Context<E>>::open(ctx, root + stem + ".tbd"))
          return file;
        if (auto *file = MappedFile<Context<E>>::open(ctx, root + path))
          return file;
      }

      if (auto *file = MappedFile<Context<E>>::open(ctx, root + path + ".tbd"))
        return file;
      if (auto *file = MappedFile<Context<E>>::open(ctx, root + path + ".dylib"))
        return file;
    }

    return nullptr;
  };

  if (path.starts_with("@executable_path/") && ctx.output_type == MH_EXECUTE) {
    path = path_clean(ctx.arg.executable_path + "/../" + path.substr(17));
    return find(path);
  }

  if (path.starts_with("@loader_path/")) {
    path = path_clean(std::string(loader.mf->name) + "/../" + path.substr(13));
    return find(path);
  }

  if (path.starts_with("@rpath/")) {
    for (std::string_view rpath : loader.rpaths) {
      std::string p = path_clean(std::string(rpath) + "/" + path.substr(6));
      if (MappedFile<Context<E>> *ret = find(p))
        return ret;
    }
    return nullptr;
  }

  return find(path);
}

template <typename E>
void DylibFile<E>::parse(Context<E> &ctx) {
  switch (get_file_type(ctx, this->mf)) {
  case FileType::TAPI:
    parse_tapi(ctx);
    break;
  case FileType::MACH_DYLIB:
    parse_dylib(ctx);
    break;
  case FileType::MACH_EXE:
    parse_dylib(ctx);
    dylib_idx = BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE;
    break;
  default:
    Fatal(ctx) << *this << ": is not a dylib";
  }

  // Read reexported libraries if any
  for (std::string_view path : reexported_libs) {
    MappedFile<Context<E>> *mf =
      find_external_lib(ctx, *this, std::string(path));
    if (!mf)
      Fatal(ctx) << install_name << ": cannot open reexported library " << path;

    DylibFile<E> *child = DylibFile<E>::create(ctx, mf);
    child->parse(ctx);

    // By default, symbols defined by re-exported libraries are handled as
    // if they were defined by the umbrella library. At runtime, the dynamic
    // linker tries to find re-exported symbols from re-exported libraries.
    // That incurs some run-time cost because the runtime has to do linear
    // search.
    //
    // As an exception, system libraries get different treatment. Their
    // symbols are directly linked against their original library names
    // even if they are re-exported to reduce the cost of runtime symbol
    // lookup. This optimization can be disable by passing `-no_implicit_dylibs`.
    if (ctx.arg.implicit_dylibs && is_system_dylib(child->install_name)) {
      hoisted_libs.push_back(child);
      child->is_alive = false;
    } else {
      for (auto [name, flags] : child->exports)
        add_export(ctx, name, flags);
      append(hoisted_libs, child->hoisted_libs);
    }
  }

  // Initialize syms
  for (auto [name, flags] : exports)
    this->syms.push_back(get_symbol(ctx, name));
}

template <typename E>
void DylibFile<E>::add_export(Context<E> &ctx, std::string_view name, u32 flags) {
  auto mask = EXPORT_SYMBOL_FLAGS_KIND_MASK;
  auto tls = EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL;
  auto weak = EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION;

  u32 &existing = exports[name];
  if (existing == 0) {
    existing = flags;
    return;
  }

  if (((existing & mask) == tls) != ((flags & mask) == tls))
    Error(ctx) << *this << ": inconsistent TLS type: " << name;

  if ((existing & weak) && !(flags & weak))
    existing = flags;
}

template <typename E>
void DylibFile<E>::read_trie(Context<E> &ctx, u8 *start, i64 offset,
                             const std::string &prefix) {
  u8 *buf = start + offset;

  if (*buf) {
    read_uleb(buf); // size
    u32 flags = read_uleb(buf);
    std::string_view name;

    if (flags & EXPORT_SYMBOL_FLAGS_REEXPORT) {
      read_uleb(buf); // skip a library ordinal
      std::string_view str((char *)buf);
      buf += str.size() + 1;
      name = !str.empty() ? str : save_string(ctx, prefix);
    } else if (flags & EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER) {
      name = save_string(ctx, prefix);
      read_uleb(buf); // stub offset
      read_uleb(buf); // resolver offset
    } else {
      name = save_string(ctx, prefix);
      read_uleb(buf); // addr
    }

    add_export(ctx, name, flags);
  } else {
    buf++;
  }

  i64 nchild = *buf++;

  for (i64 i = 0; i < nchild; i++) {
    std::string suffix((char *)buf);
    buf += suffix.size() + 1;
    i64 off = read_uleb(buf);
    assert(off != offset);
    read_trie(ctx, start, off, prefix + suffix);
  }
}

template <typename E>
void DylibFile<E>::parse_tapi(Context<E> &ctx) {
  TextDylib tbd = parse_tbd(ctx, this->mf);

  install_name = tbd.install_name;
  reexported_libs = std::move(tbd.reexported_libs);

  for (std::string_view name : tbd.exports)
    add_export(ctx, name, 0);

  for (std::string_view name : tbd.weak_exports)
    add_export(ctx, name, EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION);
}

template <typename E>
void DylibFile<E>::parse_dylib(Context<E> &ctx) {
  MachHeader &hdr = *(MachHeader *)this->mf->data;
  u8 *p = this->mf->data + sizeof(hdr);

  if (ctx.arg.application_extension && !(hdr.flags & MH_APP_EXTENSION_SAFE))
    Warn(ctx) << "linking against a dylib which is not safe for use in "
              << "application extensions: " << *this;

  for (i64 i = 0; i < hdr.ncmds; i++) {
    LoadCommand &lc = *(LoadCommand *)p;

    switch (lc.cmd) {
    case LC_ID_DYLIB: {
      DylibCommand &cmd = *(DylibCommand *)p;
      install_name = (char *)p + cmd.nameoff;
      break;
    }
    case LC_DYLD_INFO_ONLY: {
      DyldInfoCommand &cmd = *(DyldInfoCommand *)p;
      if (cmd.export_off && cmd.export_size)
        read_trie(ctx, this->mf->data + cmd.export_off, 0, "");
      break;
    }
    case LC_DYLD_EXPORTS_TRIE: {
      LinkEditDataCommand &cmd = *(LinkEditDataCommand *)p;
      read_trie(ctx, this->mf->data + cmd.dataoff, 0, "");
      break;
    }
    case LC_REEXPORT_DYLIB:
      if (!(hdr.flags & MH_NO_REEXPORTED_DYLIBS)) {
        DylibCommand &cmd = *(DylibCommand *)p;
        reexported_libs.push_back((char *)p + cmd.nameoff);
      }
      break;
    case LC_RPATH: {
      RpathCommand &cmd = *(RpathCommand *)p;
      std::string rpath = (char *)p + cmd.path_off;
      if (rpath.starts_with("@loader_path/"))
        rpath = std::string(this->mf->name) + "/../" + rpath.substr(13);
      rpaths.push_back(rpath);
      break;
    }
    }
    p += lc.cmdsize;
  }
}

template <typename E>
void DylibFile<E>::resolve_symbols(Context<E> &ctx) {
  auto it = exports.begin();

  for (i64 i = 0; i < this->syms.size(); i++) {
    Symbol<E> &sym = *this->syms[i];
    u32 flags = (*it++).second;
    u32 kind = (flags & EXPORT_SYMBOL_FLAGS_KIND_MASK);

    std::scoped_lock lock(sym.mu);

    if (get_rank(this, false, false) < get_rank(sym)) {
      sym.file = this;
      sym.visibility = SCOPE_GLOBAL;
      sym.is_weak = this->is_weak || (flags & EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION);
      sym.no_dead_strip = false;
      sym.subsec = nullptr;
      sym.value = 0;
      sym.is_common = false;
      sym.is_abs = false;
      sym.is_tlv = (kind == EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL);
    }
  }

  assert(it == exports.end());
}

template <typename E>
void DylibFile<E>::compute_symtab_size(Context<E> &ctx) {
  for (Symbol<E> *sym : this->syms) {
    if (sym && sym->file == this && (sym->stub_idx != -1 || sym->got_idx != -1)) {
      this->num_undefs++;
      this->strtab_size += sym->name.size() + 1;
      sym->output_symtab_idx = -2;
    }
  }
}

template <typename E>
void DylibFile<E>::populate_symtab(Context<E> &ctx) {
  MachSym<E> *buf = (MachSym<E> *)(ctx.buf + ctx.symtab.hdr.offset);
  u8 *strtab = ctx.buf + ctx.strtab.hdr.offset;
  i64 stroff = this->strtab_offset;

  // Copy symbols from input symtabs to the output sytmab
  for (i64 i = 0; i < this->syms.size(); i++) {
    Symbol<E> *sym = this->syms[i];
    if (!sym || sym->file != this || sym->output_symtab_idx == -1)
      continue;

    MachSym<E> &msym = buf[sym->output_symtab_idx];
    msym.stroff = stroff;
    msym.is_extern = true;
    msym.type = N_UNDF;
    msym.sect = N_UNDF;
    msym.desc = dylib_idx << 8;

    stroff += write_string(strtab + stroff, sym->name);
  }
}

using E = MOLD_TARGET;

template class InputFile<E>;
template class ObjectFile<E>;
template class DylibFile<E>;
template std::ostream &operator<<(std::ostream &, const InputFile<E> &);

} // namespace mold::macho


// 文件 output-chunks.cc
#include "mold.h"
#include "../common/sha.h"

#include <shared_mutex>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_for_each.h>
#include <tbb/parallel_sort.h>

#ifndef _WIN32
# include <sys/mman.h>
#endif

namespace mold::macho {

template <typename E>
std::ostream &operator<<(std::ostream &out, const Chunk<E> &chunk) {
  out << chunk.hdr.get_segname() << "," << chunk.hdr.get_sectname();
  return out;
}

template <typename T>
static std::vector<u8> to_u8vec(T &data) {
  std::vector<u8> buf(sizeof(T));
  memcpy(buf.data(), &data, sizeof(T));
  return buf;
}

template <typename E>
static std::vector<u8> create_pagezero_cmd(Context<E> &ctx) {
  SegmentCommand<E> cmd = {};
  cmd.cmd = LC_SEGMENT_64;
  cmd.cmdsize = sizeof(cmd);
  strcpy(cmd.segname, "__PAGEZERO");
  cmd.vmsize = ctx.arg.pagezero_size;
  return to_u8vec(cmd);
}

template <typename E>
static std::vector<u8> create_segment_cmd(Context<E> &ctx, OutputSegment<E> &seg) {
  i64 nsects = 0;
  for (Chunk<E> *sec : seg.chunks)
    if (!sec->is_hidden)
      nsects++;

  SegmentCommand<E> cmd = seg.cmd;
  cmd.cmdsize = sizeof(SegmentCommand<E>) + sizeof(MachSection<E>) * nsects;
  cmd.nsects = nsects;

  std::vector<u8> buf = to_u8vec(cmd);
  for (Chunk<E> *sec : seg.chunks)
    if (!sec->is_hidden)
      append(buf, to_u8vec(sec->hdr));
  return buf;
}

template <typename E>
static std::vector<u8> create_dyld_info_only_cmd(Context<E> &ctx) {
  DyldInfoCommand cmd = {};
  cmd.cmd = LC_DYLD_INFO_ONLY;
  cmd.cmdsize = sizeof(cmd);

  if (ctx.rebase && ctx.rebase->hdr.size) {
    cmd.rebase_off = ctx.rebase->hdr.offset;
    cmd.rebase_size = ctx.rebase->hdr.size;
  }

  if (ctx.bind && ctx.bind->hdr.size) {
    cmd.bind_off = ctx.bind->hdr.offset;
    cmd.bind_size = ctx.bind->hdr.size;
  }

  if (ctx.lazy_bind && ctx.lazy_bind->hdr.size) {
    cmd.lazy_bind_off = ctx.lazy_bind->hdr.offset;
    cmd.lazy_bind_size = ctx.lazy_bind->hdr.size;
  }

  if (ctx.export_.hdr.size) {
    cmd.export_off = ctx.export_.hdr.offset;
    cmd.export_size = ctx.export_.hdr.size;
  }
  return to_u8vec(cmd);
}

template <typename E>
static std::vector<u8> create_symtab_cmd(Context<E> &ctx) {
  SymtabCommand cmd = {};
  cmd.cmd = LC_SYMTAB;
  cmd.cmdsize = sizeof(cmd);
  cmd.symoff = ctx.symtab.hdr.offset;
  cmd.nsyms = ctx.symtab.hdr.size / sizeof(MachSym<E>);
  cmd.stroff = ctx.strtab.hdr.offset;
  cmd.strsize = ctx.strtab.hdr.size;
  return to_u8vec(cmd);
}

template <typename E>
static std::vector<u8> create_dysymtab_cmd(Context<E> &ctx) {
  DysymtabCommand cmd = {};
  cmd.cmd = LC_DYSYMTAB;
  cmd.cmdsize = sizeof(cmd);
  cmd.ilocalsym = 0;
  cmd.nlocalsym = ctx.symtab.globals_offset;
  cmd.iextdefsym = ctx.symtab.globals_offset;
  cmd.nextdefsym = ctx.symtab.undefs_offset - ctx.symtab.globals_offset;
  cmd.iundefsym = ctx.symtab.undefs_offset;
  cmd.nundefsym = ctx.symtab.hdr.size / sizeof(MachSym<E>) - ctx.symtab.undefs_offset;
  cmd.indirectsymoff = ctx.indir_symtab.hdr.offset;
  cmd.nindirectsyms  = ctx.indir_symtab.hdr.size / ctx.indir_symtab.ENTRY_SIZE;
  return to_u8vec(cmd);
}

template <typename E>
static std::vector<u8> create_dylinker_cmd(Context<E> &ctx) {
  static constexpr char path[] = "/usr/lib/dyld";

  i64 size = sizeof(DylinkerCommand) + sizeof(path);
  std::vector<u8> buf(align_to(size, 8));

  DylinkerCommand &cmd = *(DylinkerCommand *)buf.data();
  cmd.cmd = LC_LOAD_DYLINKER;
  cmd.cmdsize = buf.size();
  cmd.nameoff = sizeof(cmd);
  memcpy(buf.data() + sizeof(cmd), path, sizeof(path));
  return buf;
}

template <typename E>
static std::vector<u8> create_uuid_cmd(Context<E> &ctx) {
  UUIDCommand cmd = {};
  cmd.cmd = LC_UUID;
  cmd.cmdsize = sizeof(cmd);

  assert(sizeof(cmd.uuid) == sizeof(ctx.uuid));
  memcpy(cmd.uuid, ctx.uuid, sizeof(cmd.uuid));
  return to_u8vec(cmd);
}

template <typename E>
static std::vector<u8> create_build_version_cmd(Context<E> &ctx) {
  i64 size = sizeof(BuildVersionCommand) + sizeof(BuildToolVersion);
  std::vector<u8> buf(align_to(size, 8));

  BuildVersionCommand &cmd = *(BuildVersionCommand *)buf.data();
  cmd.cmd = LC_BUILD_VERSION;
  cmd.cmdsize = buf.size();
  cmd.platform = ctx.arg.platform;
  cmd.minos = ctx.arg.platform_min_version.encode();
  cmd.sdk = ctx.arg.platform_sdk_version.encode();
  cmd.ntools = 1;

  BuildToolVersion &tool = *(BuildToolVersion *)(buf.data() + sizeof(cmd));
  tool.tool = TOOL_MOLD;
  tool.version = parse_version(ctx, mold_version_string).encode();
  return buf;
}

template <typename E>
static std::vector<u8> create_source_version_cmd(Context<E> &ctx) {
  SourceVersionCommand cmd = {};
  cmd.cmd = LC_SOURCE_VERSION;
  cmd.cmdsize = sizeof(cmd);
  return to_u8vec(cmd);
}

template <typename E>
static std::vector<u8> create_main_cmd(Context<E> &ctx) {
  EntryPointCommand cmd = {};
  cmd.cmd = LC_MAIN;
  cmd.cmdsize = sizeof(cmd);
  cmd.entryoff = ctx.arg.entry->get_addr(ctx) - ctx.mach_hdr.hdr.addr;
  cmd.stacksize = ctx.arg.stack_size;
  return to_u8vec(cmd);
}

template <typename E>
static std::vector<u8>
create_load_dylib_cmd(Context<E> &ctx, DylibFile<E> &dylib) {
  i64 size = sizeof(DylibCommand) + dylib.install_name.size() + 1; // +1 for NUL
  std::vector<u8> buf(align_to(size, 8));

  auto get_type = [&] {
    if (dylib.is_reexported)
      return LC_REEXPORT_DYLIB;
    if (dylib.is_weak)
      return LC_LOAD_WEAK_DYLIB;
    return LC_LOAD_DYLIB;
  };

  DylibCommand &cmd = *(DylibCommand *)buf.data();
  cmd.cmd = get_type();
  cmd.cmdsize = buf.size();
  cmd.nameoff = sizeof(cmd);
  cmd.timestamp = 2;
  cmd.current_version = ctx.arg.current_version.encode();
  cmd.compatibility_version = ctx.arg.compatibility_version.encode();
  write_string(buf.data() + sizeof(cmd), dylib.install_name);
  return buf;
}

template <typename E>
static std::vector<u8> create_rpath_cmd(Context<E> &ctx, std::string_view name) {
  i64 size = sizeof(RpathCommand) + name.size() + 1; // +1 for NUL
  std::vector<u8> buf(align_to(size, 8));

  RpathCommand &cmd = *(RpathCommand *)buf.data();
  cmd.cmd = LC_RPATH;
  cmd.cmdsize = buf.size();
  cmd.path_off = sizeof(cmd);
  write_string(buf.data() + sizeof(cmd), name);
  return buf;
}

template <typename E>
static std::vector<u8> create_function_starts_cmd(Context<E> &ctx) {
  LinkEditDataCommand cmd = {};
  cmd.cmd = LC_FUNCTION_STARTS;
  cmd.cmdsize = sizeof(cmd);
  cmd.dataoff = ctx.function_starts->hdr.offset;
  cmd.datasize = ctx.function_starts->hdr.size;
  return to_u8vec(cmd);
}

template <typename E>
static std::vector<u8> create_data_in_code_cmd(Context<E> &ctx) {
  LinkEditDataCommand cmd = {};
  cmd.cmd = LC_DATA_IN_CODE;
  cmd.cmdsize = sizeof(cmd);
  cmd.dataoff = ctx.data_in_code->hdr.offset;
  cmd.datasize = ctx.data_in_code->hdr.size;
  return to_u8vec(cmd);
}

template <typename E>
static std::vector<u8> create_dyld_chained_fixups(Context<E> &ctx) {
  LinkEditDataCommand cmd = {};
  cmd.cmd = LC_DYLD_CHAINED_FIXUPS;
  cmd.cmdsize = sizeof(cmd);
  cmd.dataoff = ctx.chained_fixups->hdr.offset;
  cmd.datasize = ctx.chained_fixups->hdr.size;
  return to_u8vec(cmd);
}

template <typename E>
static std::vector<u8> create_dyld_exports_trie(Context<E> &ctx) {
  LinkEditDataCommand cmd = {};
  cmd.cmd = LC_DYLD_EXPORTS_TRIE;
  cmd.cmdsize = sizeof(cmd);
  cmd.dataoff = ctx.export_.hdr.offset;
  cmd.datasize = ctx.export_.hdr.size;
  return to_u8vec(cmd);
}

template <typename E>
static std::vector<u8> create_sub_framework_cmd(Context<E> &ctx) {
  i64 size = sizeof(UmbrellaCommand) + ctx.arg.umbrella.size() + 1;
  std::vector<u8> buf(align_to(size, 8));

  UmbrellaCommand &cmd = *(UmbrellaCommand *)buf.data();
  cmd.cmd = LC_SUB_FRAMEWORK;
  cmd.cmdsize = buf.size();
  cmd.umbrella_off = sizeof(cmd);
  write_string(buf.data() + sizeof(cmd), ctx.arg.umbrella);
  return buf;
}

template <typename E>
static std::vector<u8> create_id_dylib_cmd(Context<E> &ctx) {
  i64 size = sizeof(DylibCommand) + ctx.arg.final_output.size() + 1;
  std::vector<u8> buf(align_to(size, 8));

  DylibCommand &cmd = *(DylibCommand *)buf.data();
  cmd.cmd = LC_ID_DYLIB;
  cmd.cmdsize = buf.size();
  cmd.nameoff = sizeof(cmd);
  write_string(buf.data() + sizeof(cmd), ctx.arg.final_output);
  return buf;
}

template <typename E>
static std::vector<u8> create_code_signature_cmd(Context<E> &ctx) {
  LinkEditDataCommand cmd = {};
  cmd.cmd = LC_CODE_SIGNATURE;
  cmd.cmdsize = sizeof(cmd);
  cmd.dataoff = ctx.code_sig->hdr.offset;
  cmd.datasize = ctx.code_sig->hdr.size;
  return to_u8vec(cmd);
}

template <typename E>
static std::vector<std::vector<u8>> create_load_commands(Context<E> &ctx) {
  std::vector<std::vector<u8>> vec;

  if (ctx.arg.pagezero_size)
    vec.push_back(create_pagezero_cmd(ctx));

  for (std::unique_ptr<OutputSegment<E>> &seg : ctx.segments)
    vec.push_back(create_segment_cmd(ctx, *seg));

  if (ctx.chained_fixups && ctx.chained_fixups->hdr.size) {
    vec.push_back(create_dyld_chained_fixups(ctx));
    if (ctx.export_.hdr.size)
      vec.push_back(create_dyld_exports_trie(ctx));
  } else {
    vec.push_back(create_dyld_info_only_cmd(ctx));
  }

  vec.push_back(create_symtab_cmd(ctx));
  vec.push_back(create_dysymtab_cmd(ctx));

  if (ctx.arg.uuid != UUID_NONE)
    vec.push_back(create_uuid_cmd(ctx));

  vec.push_back(create_build_version_cmd(ctx));
  vec.push_back(create_source_version_cmd(ctx));

  if (ctx.arg.function_starts)
    vec.push_back(create_function_starts_cmd(ctx));

  for (DylibFile<E> *file : ctx.dylibs)
    if (file->dylib_idx != BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE)
      vec.push_back(create_load_dylib_cmd(ctx, *file));

  for (std::string_view rpath : ctx.arg.rpaths)
    vec.push_back(create_rpath_cmd(ctx, rpath));

  if (ctx.data_in_code)
    vec.push_back(create_data_in_code_cmd(ctx));

  if (!ctx.arg.umbrella.empty())
    vec.push_back(create_sub_framework_cmd(ctx));

  switch (ctx.output_type) {
  case MH_EXECUTE:
    vec.push_back(create_dylinker_cmd(ctx));
    vec.push_back(create_main_cmd(ctx));
    break;
  case MH_DYLIB:
    vec.push_back(create_id_dylib_cmd(ctx));
    break;
  case MH_BUNDLE:
    break;
  default:
    unreachable();
  }

  if (ctx.code_sig)
    vec.push_back(create_code_signature_cmd(ctx));
  return vec;
}

template <typename E>
void OutputMachHeader<E>::compute_size(Context<E> &ctx) {
  std::vector<std::vector<u8>> cmds = create_load_commands(ctx);
  this->hdr.size = sizeof(MachHeader) + flatten(cmds).size() + ctx.arg.headerpad;
}

template <typename E>
static bool has_tlv(Context<E> &ctx) {
  for (std::unique_ptr<OutputSegment<E>> &seg : ctx.segments)
    for (Chunk<E> *chunk : seg->chunks)
      if (chunk->hdr.type == S_THREAD_LOCAL_VARIABLES)
        return true;
  return false;
}

template <typename E>
static bool has_reexported_lib(Context<E> &ctx) {
  for (DylibFile<E> *file : ctx.dylibs)
    if (file->is_reexported)
      return true;
  return false;
}

template <typename E>
void OutputMachHeader<E>::copy_buf(Context<E> &ctx) {
  u8 *buf = ctx.buf + this->hdr.offset;

  std::vector<std::vector<u8>> cmds = create_load_commands(ctx);

  MachHeader &mhdr = *(MachHeader *)buf;
  mhdr.magic = 0xfeedfacf;
  mhdr.cputype = E::cputype;
  mhdr.cpusubtype = E::cpusubtype;
  mhdr.filetype = ctx.output_type;
  mhdr.ncmds = cmds.size();
  mhdr.sizeofcmds = flatten(cmds).size();
  mhdr.flags = MH_TWOLEVEL | MH_NOUNDEFS | MH_DYLDLINK | MH_PIE;

  if (has_tlv(ctx))
    mhdr.flags |= MH_HAS_TLV_DESCRIPTORS;

  if (ctx.output_type == MH_DYLIB && !has_reexported_lib(ctx))
    mhdr.flags |= MH_NO_REEXPORTED_DYLIBS;

  if (ctx.arg.mark_dead_strippable_dylib)
    mhdr.flags |= MH_DEAD_STRIPPABLE_DYLIB;

  if (ctx.arg.application_extension)
    mhdr.flags |= MH_APP_EXTENSION_SAFE;

  write_vector(buf + sizeof(mhdr), flatten(cmds));
}

template <typename E>
OutputSection<E> *
OutputSection<E>::get_instance(Context<E> &ctx, std::string_view segname,
                               std::string_view sectname) {
  static std::shared_mutex mu;

  auto find = [&]() -> OutputSection<E> * {
    for (Chunk<E> *chunk : ctx.chunks) {
      if (chunk->hdr.match(segname, sectname)) {
        if (OutputSection<E> *osec = chunk->to_osec())
          return osec;
        Fatal(ctx) << "reserved name is used: " << segname << "," << sectname;
      }
    }
    return nullptr;
  };

  {
    std::shared_lock lock(mu);
    if (OutputSection<E> *osec = find())
      return osec;
  }

  std::unique_lock lock(mu);
  if (OutputSection<E> *osec = find())
    return osec;

  OutputSection<E> *osec = new OutputSection<E>(ctx, segname, sectname);
  ctx.chunk_pool.emplace_back(osec);
  return osec;
}

template <typename E>
void OutputSection<E>::compute_size(Context<E> &ctx) {
  if constexpr (is_arm<E>) {
    if (this->hdr.attr & S_ATTR_SOME_INSTRUCTIONS ||
        this->hdr.attr & S_ATTR_PURE_INSTRUCTIONS) {
      create_range_extension_thunks(ctx, *this);
      return;
    }
  }

  // As a special case, we need a word-size padding at the beginning
  // of __data for dyld. It is located by __dyld_private symbol.
  u64 offset = (this == ctx.data) ? sizeof(Word<E>) : 0;

  for (Subsection<E> *subsec : members) {
    offset = align_to(offset, 1 << subsec->p2align);
    subsec->output_offset = offset;
    offset += subsec->input_size;
  }
  this->hdr.size = offset;
}

template <typename E>
void OutputSection<E>::copy_buf(Context<E> &ctx) {
  assert(this->hdr.type != S_ZEROFILL);

  tbb::parallel_for_each(members, [&](Subsection<E> *subsec) {
    std::string_view data = subsec->get_contents();
    u8 *loc = ctx.buf + this->hdr.offset + subsec->output_offset;
    memcpy(loc, data.data(), data.size());
    subsec->apply_reloc(ctx, loc);
  });

  if constexpr (is_arm<E>) {
    tbb::parallel_for_each(thunks,
                           [&](std::unique_ptr<RangeExtensionThunk<E>> &thunk) {
      thunk->copy_buf(ctx);
    });
  }
}

template <typename E>
OutputSegment<E> *
OutputSegment<E>::get_instance(Context<E> &ctx, std::string_view name) {
  static std::shared_mutex mu;

  auto find = [&]() -> OutputSegment<E> *{
    for (std::unique_ptr<OutputSegment<E>> &seg : ctx.segments)
      if (seg->cmd.get_segname() == name)
        return seg.get();
    return nullptr;
  };

  {
    std::shared_lock lock(mu);
    if (OutputSegment<E> *seg = find())
      return seg;
  }

  std::unique_lock lock(mu);
  if (OutputSegment<E> *seg = find())
    return seg;

  OutputSegment<E> *seg = new OutputSegment<E>(name);
  ctx.segments.emplace_back(seg);
  return seg;
}

template <typename E>
OutputSegment<E>::OutputSegment(std::string_view name) {
  cmd.cmd = LC_SEGMENT_64;
  memcpy(cmd.segname, name.data(), name.size());

  if (name == "__PAGEZERO")
    cmd.initprot = 0;
  else if (name == "__TEXT")
    cmd.initprot = VM_PROT_READ | VM_PROT_EXECUTE;
  else if (name == "__LINKEDIT")
    cmd.initprot = VM_PROT_READ;
  else
    cmd.initprot = VM_PROT_READ | VM_PROT_WRITE;

  cmd.maxprot = cmd.initprot;

  if (name == "__DATA_CONST")
    cmd.flags = SG_READ_ONLY;
}

template <typename E>
void OutputSegment<E>::set_offset(Context<E> &ctx, i64 fileoff, u64 vmaddr) {
  cmd.fileoff = fileoff;
  cmd.vmaddr = vmaddr;

  if (cmd.get_segname() == "__LINKEDIT")
    set_offset_linkedit(ctx, fileoff, vmaddr);
  else
    set_offset_regular(ctx, fileoff, vmaddr);
}

template <typename E>
void OutputSegment<E>::set_offset_regular(Context<E> &ctx, i64 fileoff,
                                          u64 vmaddr) {
  Timer t(ctx, std::string(cmd.get_segname()));
  i64 i = 0;

  auto is_bss = [](Chunk<E> &x) {
    return x.hdr.type == S_ZEROFILL || x.hdr.type == S_THREAD_LOCAL_ZEROFILL;
  };

  auto get_tls_alignment = [&] {
    i64 val = 1;
    for (Chunk<E> *chunk : chunks)
      if (chunk->hdr.type == S_THREAD_LOCAL_REGULAR ||
          chunk->hdr.type == S_THREAD_LOCAL_ZEROFILL)
        val = std::max<i64>(val, 1 << chunk->hdr.p2align);
    return val;
  };

  auto get_alignment = [&](Chunk<E> &chunk) -> u32 {
    switch (chunk.hdr.type) {
    case S_THREAD_LOCAL_REGULAR:
    case S_THREAD_LOCAL_ZEROFILL:
      // A TLS initialization image is copied as a contiguous block, so
      // the alignment of it is the largest of __thread_data and
      // __thread_bss.  This function returns an alignment value of a TLS
      // initialization image.
      return get_tls_alignment();
    case S_THREAD_LOCAL_VARIABLES:
      // __thread_vars needs to be aligned to word size because it
      // contains pointers. For some reason, Apple's clang creates it with
      // an alignment of 1. So we need to override.
      return sizeof(Word<E>);
    default:
      return 1 << chunk.hdr.p2align;
    }
  };

  // Assign offsets to non-BSS sections
  while (i < chunks.size() && !is_bss(*chunks[i])) {
    Timer t2(ctx, std::string(chunks[i]->hdr.get_sectname()), &t);
    Chunk<E> &sec = *chunks[i++];

    fileoff = align_to(fileoff, get_alignment(sec));
    vmaddr = align_to(vmaddr, get_alignment(sec));

    sec.hdr.offset = fileoff;
    sec.hdr.addr = vmaddr;

    sec.compute_size(ctx);
    fileoff += sec.hdr.size;
    vmaddr += sec.hdr.size;
  }

  // Assign offsets to BSS sections
  while (i < chunks.size()) {
    Chunk<E> &sec = *chunks[i++];
    assert(is_bss(sec));

    vmaddr = align_to(vmaddr, get_alignment(sec));
    sec.hdr.addr = vmaddr;
    sec.compute_size(ctx);
    vmaddr += sec.hdr.size;
  }

  cmd.vmsize = align_to(vmaddr - cmd.vmaddr, E::page_size);
  cmd.filesize = align_to(fileoff - cmd.fileoff, E::page_size);
}

template <typename E>
void OutputSegment<E>::set_offset_linkedit(Context<E> &ctx, i64 fileoff,
                                           u64 vmaddr) {
  Timer t(ctx, "__LINKEDIT");

  // Unlike regular segments, __LINKEDIT member sizes can be computed in
  // parallel except __string_table and __code_signature sections.
  auto skip = [&](Chunk<E> *c) {
    return c == &ctx.strtab || c == ctx.code_sig.get();
  };

  tbb::parallel_for_each(chunks, [&](Chunk<E> *chunk) {
    if (!skip(chunk)) {
      Timer t2(ctx, std::string(chunk->hdr.get_sectname()), &t);
      chunk->compute_size(ctx);
    }
  });

  for (Chunk<E> *chunk : chunks) {
    fileoff = align_to(fileoff, 1 << chunk->hdr.p2align);
    vmaddr = align_to(vmaddr, 1 << chunk->hdr.p2align);

    chunk->hdr.offset = fileoff;
    chunk->hdr.addr = vmaddr;

    if (skip(chunk)) {
      Timer t2(ctx, std::string(chunk->hdr.get_sectname()), &t);
      chunk->compute_size(ctx);
    }

    fileoff += chunk->hdr.size;
    vmaddr += chunk->hdr.size;
  }

  cmd.vmsize = align_to(vmaddr - cmd.vmaddr, E::page_size);
  cmd.filesize = fileoff - cmd.fileoff;
}

struct RebaseEntry {
  RebaseEntry(const RebaseEntry &) = default;
  RebaseEntry(i32 seg_idx, i32 offset) : seg_idx(seg_idx), offset(offset) {}
  auto operator<=>(const RebaseEntry &) const = default;

  i32 seg_idx;
  i32 offset;
};

static std::vector<u8> encode_rebase_entries(std::vector<RebaseEntry> &rebases) {
  std::vector<u8> buf;
  buf.push_back(REBASE_OPCODE_SET_TYPE_IMM | REBASE_TYPE_POINTER);

  // Sort rebase entries to reduce the size of the output
  tbb::parallel_sort(rebases);

  // Emit rebase records
  for (i64 i = 0; i < rebases.size();) {
    RebaseEntry &cur = rebases[i];
    RebaseEntry *last = (i == 0) ? nullptr : &rebases[i - 1];

    // Write a segment index and an offset
    if (!last || last->seg_idx != cur.seg_idx ||
        cur.offset - last->offset - 8 < 0) {
      buf.push_back(REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | cur.seg_idx);
      encode_uleb(buf, cur.offset);
    } else {
      i64 dist = cur.offset - last->offset - 8;
      assert(dist >= 0);

      if (dist % 8 == 0 && dist < 128) {
        buf.push_back(REBASE_OPCODE_ADD_ADDR_IMM_SCALED | (dist >> 3));
      } else {
        buf.push_back(REBASE_OPCODE_ADD_ADDR_ULEB);
        encode_uleb(buf, dist);
      }
    }

    // Advance j so that j refers to past of the end of consecutive relocs
    i64 j = i + 1;
    while (j < rebases.size() &&
           rebases[j - 1].seg_idx == rebases[j].seg_idx &&
           rebases[j - 1].offset + 8 == rebases[j].offset)
      j++;

    // Write the consecutive relocs
    if (j - i < 16) {
      buf.push_back(REBASE_OPCODE_DO_REBASE_IMM_TIMES | (j - i));
    } else {
      buf.push_back(REBASE_OPCODE_DO_REBASE_ULEB_TIMES);
      encode_uleb(buf, j - i);
    }

    i = j;
  }

  buf.push_back(REBASE_OPCODE_DONE);
  buf.resize(align_to(buf.size(), 8));
  return buf;
}

template <typename E>
static bool needs_rebasing(const Relocation<E> &r) {
  // Rebase only ARM64_RELOC_UNSIGNED or X86_64_RELOC_UNSIGNED relocs.
  if (r.type != E::abs_rel)
    return false;

  // If the reloc specifies the relative address between two relocations,
  // we don't need a rebase reloc.
  if (r.is_subtracted)
    return false;

  // If we have a dynamic reloc, we don't need to rebase it.
  if (r.sym() && r.sym()->is_imported)
    return false;

  // If it refers a TLS block, it's already relative to the thread
  // pointer, so it doesn't have to be adjusted to the loaded address.
  if (r.refers_to_tls())
    return false;

  return true;
}

template <typename E>
inline void RebaseSection<E>::compute_size(Context<E> &ctx) {
  std::vector<std::vector<RebaseEntry>> vec(ctx.objs.size());

  tbb::parallel_for((i64)0, (i64)ctx.objs.size(), [&](i64 i) {
    for (Subsection<E> *subsec : ctx.objs[i]->subsections) {
      if (!subsec->is_alive)
        continue;

      std::span<Relocation<E>> rels = subsec->get_rels();
      OutputSegment<E> &seg = *subsec->isec->osec.seg;
      i64 base = subsec->get_addr(ctx) - seg.cmd.vmaddr;

      for (Relocation<E> &rel : rels)
        if (needs_rebasing(rel))
          vec[i].emplace_back(seg.seg_idx, base + rel.offset);
    }
  });

  std::vector<RebaseEntry> rebases = flatten(vec);

  for (i64 i = 0; Symbol<E> *sym : ctx.stubs.syms)
    if (!sym->has_got())
      rebases.emplace_back(ctx.data_seg->seg_idx,
                           ctx.lazy_symbol_ptr->hdr.addr + i++ * sizeof(Word<E>) -
                           ctx.data_seg->cmd.vmaddr);

  for (Symbol<E> *sym : ctx.got.syms)
    if (!sym->is_imported)
      rebases.emplace_back(ctx.data_const_seg->seg_idx,
                           sym->get_got_addr(ctx) - ctx.data_const_seg->cmd.vmaddr);

  for (Symbol<E> *sym : ctx.thread_ptrs.syms)
    if (!sym->is_imported)
      rebases.emplace_back(ctx.data_seg->seg_idx,
                           sym->get_tlv_addr(ctx) - ctx.data_seg->cmd.vmaddr);

  contents = encode_rebase_entries(rebases);
  this->hdr.size = contents.size();
}

template <typename E>
inline void RebaseSection<E>::copy_buf(Context<E> &ctx) {
  write_vector(ctx.buf + this->hdr.offset, contents);
}

template <typename E>
struct BindEntry {
  BindEntry(const BindEntry &) = default;
  BindEntry(Symbol<E> *sym, i32 seg_idx, i32 offset, i64 addend)
    : sym(sym), seg_idx(seg_idx), offset(offset), addend(addend) {}

  Symbol<E> *sym;
  i32 seg_idx;
  i32 offset;
  i64 addend;
};

template <typename E>
static i32 get_dylib_idx(Context<E> &ctx, Symbol<E> &sym) {
  assert(sym.is_imported);

  if (ctx.arg.flat_namespace)
    return BIND_SPECIAL_DYLIB_FLAT_LOOKUP;

  if (sym.file->is_dylib)
    return ((DylibFile<E> *)sym.file)->dylib_idx;

  assert(!ctx.arg.U.empty());
  return BIND_SPECIAL_DYLIB_FLAT_LOOKUP;
}

template <typename E>
std::vector<u8>
encode_bind_entries(Context<E> &ctx, std::vector<BindEntry<E>> &bindings) {
  std::vector<u8> buf;
  buf.push_back(BIND_OPCODE_SET_TYPE_IMM | BIND_TYPE_POINTER);

  // Sort the vector to minimize the encoded binding info size.
  sort(bindings, [](const BindEntry<E> &a, const BindEntry<E> &b) {
    return std::tuple(a.sym->name, a.seg_idx, a.offset, a.addend) <
           std::tuple(b.sym->name, b.seg_idx, b.offset, b.addend);
  });

  // Encode bindings
  for (i64 i = 0; i < bindings.size(); i++) {
    BindEntry<E> &b = bindings[i];
    BindEntry<E> *last = (i == 0) ? nullptr : &bindings[i - 1];

    if (!last || b.sym->file != last->sym->file) {
      i64 idx = get_dylib_idx(ctx, *b.sym);
      if (idx < 0) {
        buf.push_back(BIND_OPCODE_SET_DYLIB_SPECIAL_IMM |
                      (idx & BIND_IMMEDIATE_MASK));
      } else if (idx < 16) {
        buf.push_back(BIND_OPCODE_SET_DYLIB_ORDINAL_IMM | idx);
      } else {
        buf.push_back(BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB);
        encode_uleb(buf, idx);
      }
    }

    if (!last || last->sym->name != b.sym->name ||
        last->sym->is_weak != b.sym->is_weak) {
      i64 flags = (b.sym->is_weak ? BIND_SYMBOL_FLAGS_WEAK_IMPORT : 0);
      buf.push_back(BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM | flags);

      std::string_view name = b.sym->name;
      buf.insert(buf.end(), (u8 *)name.data(), (u8 *)(name.data() + name.size()));
      buf.push_back('\0');
    }

    if (!last || last->seg_idx != b.seg_idx || last->offset != b.offset) {
      assert(b.seg_idx < 16);
      buf.push_back(BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | b.seg_idx);
      encode_uleb(buf, b.offset);
    }

    if (!last || last->addend != b.addend) {
      buf.push_back(BIND_OPCODE_SET_ADDEND_SLEB);
      encode_sleb(buf, b.addend);
    }

    buf.push_back(BIND_OPCODE_DO_BIND);
  }

  buf.push_back(BIND_OPCODE_DONE);
  buf.resize(align_to(buf.size(), 8));
  return buf;
}

template <typename E>
void BindSection<E>::compute_size(Context<E> &ctx) {
  std::vector<std::vector<BindEntry<E>>> vec(ctx.objs.size());

  tbb::parallel_for((i64)0, (i64)ctx.objs.size(), [&](i64 i) {
    for (Subsection<E> *subsec : ctx.objs[i]->subsections) {
      if (subsec->is_alive) {
        for (Relocation<E> &r : subsec->get_rels()) {
          if (r.type == E::abs_rel && r.sym() && r.sym()->is_imported) {
            OutputSegment<E> &seg = *subsec->isec->osec.seg;
            vec[i].emplace_back(r.sym(), seg.seg_idx,
                                subsec->get_addr(ctx) + r.offset - seg.cmd.vmaddr,
                                r.addend);
          }
        }
      }
    }
  });

  std::vector<BindEntry<E>> bindings = flatten(vec);

  for (Symbol<E> *sym : ctx.got.syms)
    if (sym->is_imported)
      bindings.emplace_back(sym, ctx.data_const_seg->seg_idx,
                            sym->get_got_addr(ctx) - ctx.data_const_seg->cmd.vmaddr,
                            0);

  for (Symbol<E> *sym : ctx.thread_ptrs.syms)
    if (sym->is_imported)
      bindings.emplace_back(sym, ctx.data_seg->seg_idx,
                            sym->get_tlv_addr(ctx) - ctx.data_seg->cmd.vmaddr, 0);

  contents = encode_bind_entries(ctx, bindings);
  this->hdr.size = contents.size();
}

template <typename E>
void BindSection<E>::copy_buf(Context<E> &ctx) {
  write_vector(ctx.buf + this->hdr.offset, contents);
}

template <typename E>
void LazyBindSection<E>::add(Context<E> &ctx, Symbol<E> &sym, i64 idx) {
  auto emit = [&](u8 byte) {
    contents.push_back(byte);
  };

  i64 dylib_idx = get_dylib_idx(ctx, sym);

  if (dylib_idx < 0) {
    emit(BIND_OPCODE_SET_DYLIB_SPECIAL_IMM | (dylib_idx & BIND_IMMEDIATE_MASK));
  } else if (dylib_idx < 16) {
    emit(BIND_OPCODE_SET_DYLIB_ORDINAL_IMM | dylib_idx);
  } else {
    emit(BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB);
    encode_uleb(contents, dylib_idx);
  }

  i64 flags = (sym.is_weak ? BIND_SYMBOL_FLAGS_WEAK_IMPORT : 0);
  assert(flags < 16);

  emit(BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM | flags);
  contents.insert(contents.end(), (u8 *)sym.name.data(),
                  (u8 *)(sym.name.data() + sym.name.size()));
  emit('\0');

  i64 seg_idx = ctx.data_seg->seg_idx;
  emit(BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | seg_idx);

  i64 offset = ctx.lazy_symbol_ptr->hdr.addr + idx * sizeof(Word<E>) -
               ctx.data_seg->cmd.vmaddr;
  encode_uleb(contents, offset);

  emit(BIND_OPCODE_DO_BIND);
  emit(BIND_OPCODE_DONE);
}

template <typename E>
void LazyBindSection<E>::compute_size(Context<E> &ctx) {
  bind_offsets.clear();

  for (i64 i = 0; i < ctx.stubs.syms.size(); i++) {
    bind_offsets.push_back(contents.size());
    add(ctx, *ctx.stubs.syms[i], i);
  }

  contents.resize(align_to(contents.size(), 1 << this->hdr.p2align));
  this->hdr.size = contents.size();
}

template <typename E>
void LazyBindSection<E>::copy_buf(Context<E> &ctx) {
  write_vector(ctx.buf + this->hdr.offset, contents);
}

inline i64 ExportEncoder::finish() {
  tbb::parallel_sort(entries, [](const Entry &a, const Entry &b) {
    return a.name < b.name;
  });

  // Construct a trie
  TrieNode node;
  tbb::task_group tg;
  construct_trie(node, entries, 0, &tg, entries.size() / 32, true);
  tg.wait();

  if (node.prefix.empty())
    root = std::move(node);
  else
    root.children.emplace_back(new TrieNode(std::move(node)));

  // Set output offsets to trie nodes. Since a serialized trie node
  // contains output offsets of other nodes in the variable-length
  // ULEB format, it unfortunately needs more than one iteration.
  // We need to repeat until the total size of the serialized trie
  // converges to obtain the optimized output. However, in reality,
  // repeating this step twice is enough. Size reduction on third and
  // further iterations is negligible.
  set_offset(root, 0);
  return set_offset(root, 0);
}

static i64 common_prefix_len(std::string_view x, std::string_view y) {
  i64 i = 0;
  while (i < x.size() && i < y.size() && x[i] == y[i])
    i++;
  return i;
}

void
inline ExportEncoder::construct_trie(TrieNode &node, std::span<Entry> entries,
                                     i64 len, tbb::task_group *tg,
                                     i64 grain_size, bool divide) {
  i64 new_len = common_prefix_len(entries[0].name, entries.back().name);

  if (new_len > len) {
    node.prefix = entries[0].name.substr(len, new_len - len);
    if (entries[0].name.size() == new_len) {
      node.is_leaf = true;
      node.flags = entries[0].flags;
      node.addr = entries[0].addr;
      entries = entries.subspan(1);
    }
  }

  for (i64 i = 0; i < entries.size();) {
    auto it = std::partition_point(entries.begin() + i + 1, entries.end(),
                                   [&](const Entry &ent) {
      return entries[i].name[new_len] == ent.name[new_len];
    });
    i64 j = it - entries.begin();

    TrieNode *child = new TrieNode;
    std::span<Entry> subspan = entries.subspan(i, j - i);

    if (divide && j - i < grain_size) {
      tg->run([=, this] {
        construct_trie(*child, subspan, new_len, tg, grain_size, false);
      });
    } else {
      construct_trie(*child, subspan, new_len, tg, grain_size, divide);
    }

    node.children.emplace_back(child);
    i = j;
  }
}

inline i64 ExportEncoder::set_offset(TrieNode &node, i64 offset) {
  node.offset = offset;

  i64 size = 0;
  if (node.is_leaf) {
    size = uleb_size(node.flags) + uleb_size(node.addr);
    size += uleb_size(size);
  } else {
    size = 1;
  }

  size++; // # of children

  for (std::unique_ptr<TrieNode> &child : node.children) {
    // +1 for NUL byte
    size += child->prefix.size() + 1 + uleb_size(child->offset);
  }

  for (std::unique_ptr<TrieNode> &child : node.children)
    size += set_offset(*child, offset + size);
  return size;
}

inline void ExportEncoder::write_trie(u8 *start, TrieNode &node) {
  u8 *buf = start + node.offset;

  if (node.is_leaf) {
    buf += write_uleb(buf, uleb_size(node.flags) + uleb_size(node.addr));
    buf += write_uleb(buf, node.flags);
    buf += write_uleb(buf, node.addr);
  } else {
    *buf++ = 0;
  }

  *buf++ = node.children.size();

  for (std::unique_ptr<TrieNode> &child : node.children) {
    buf += write_string(buf, child->prefix);
    buf += write_uleb(buf, child->offset);
  }

  for (std::unique_ptr<TrieNode> &child : node.children)
    write_trie(start, *child);
}

template <typename E>
void ExportSection<E>::compute_size(Context<E> &ctx) {
  auto get_flags = [](Symbol<E> &sym) {
    u32 flags = 0;
    if (sym.is_weak)
      flags |= EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION;
    if (sym.is_tlv)
      flags |= EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL;
    return flags;
  };

  for (ObjectFile<E> *file : ctx.objs)
    for (Symbol<E> *sym : file->syms)
      if (sym && sym->file == file && sym->visibility == SCOPE_GLOBAL)
        enc.entries.push_back({sym->name, get_flags(*sym),
                               sym->get_addr(ctx) - ctx.mach_hdr.hdr.addr});

  if (enc.entries.empty())
    return;

  this->hdr.size = align_to(enc.finish(), 8);
}

template <typename E>
void ExportSection<E>::copy_buf(Context<E> &ctx) {
  if (this->hdr.size == 0)
    return;

  u8 *buf = ctx.buf + this->hdr.offset;
  memset(buf, 0, this->hdr.size);
  enc.write_trie(buf, enc.root);
}

// LC_FUNCTION_STARTS contains function start addresses encoded in
// ULEB128. I don't know what tools consume this table, but we create
// it anyway by default for the sake of compatibility.
template <typename E>
void FunctionStartsSection<E>::compute_size(Context<E> &ctx) {
  std::vector<std::vector<u64>> vec(ctx.objs.size());

  tbb::parallel_for((i64)0, (i64)ctx.objs.size(), [&](i64 i) {
    ObjectFile<E> &file = *ctx.objs[i];
    for (Symbol<E> *sym : file.syms)
      if (sym && sym->file == &file && sym->subsec && sym->subsec->is_alive &&
          &sym->subsec->isec->osec == ctx.text)
        vec[i].push_back(sym->get_addr(ctx));
  });

  std::vector<u64> addrs = flatten(vec);
  tbb::parallel_sort(addrs.begin(), addrs.end());
  remove_duplicates(addrs);

  // We need a NUL terminator at the end. We also want to make sure that
  // the size is a multiple of 8 because the `strip` command assumes that
  // there's no gap between __func_starts and the following __data_in_code.
  contents.resize(align_to(addrs.size() * 5 + 1, 8));

  u8 *p = contents.data();
  u64 last = ctx.mach_hdr.hdr.addr;

  for (u64 val : addrs) {
    p += write_uleb(p, val - last);
    last = val;
  }

  // Write the terminator
  p += write_uleb(p, 0);

  this->hdr.size = align_to(p - contents.data(), 8);
  contents.resize(this->hdr.size);
}

template <typename E>
void FunctionStartsSection<E>::copy_buf(Context<E> &ctx) {
  write_vector(ctx.buf + this->hdr.offset, contents);
}

// The symbol table in an output file is sorted by symbol type (local,
// global or undef).
template <typename E>
void SymtabSection<E>::compute_size(Context<E> &ctx) {
  std::string cwd = std::filesystem::current_path().string();

  std::vector<InputFile<E> *> vec;
  append(vec, ctx.objs);
  append(vec, ctx.dylibs);

  // Compute the number of symbols for each symbol type
  tbb::parallel_for_each(vec, [&](InputFile<E> *file) {
    file->compute_symtab_size(ctx);
  });

  // Compute the indices in the symbol table
  InputFile<E> &first = *vec.front();
  InputFile<E> &last = *vec.back();

  // Add -add_ast_path symbols first
  first.stabs_offset = ctx.arg.add_ast_path.size();
  first.strtab_offset = strtab_init_image.size();
  for (std::string_view s : ctx.arg.add_ast_path)
    first.strtab_offset += s.size() + 1;

  // Add input file symbols
  for (i64 i = 1; i < vec.size(); i++)
    vec[i]->stabs_offset = vec[i - 1]->stabs_offset + vec[i - 1]->num_stabs;

  first.locals_offset = last.stabs_offset + last.num_stabs;

  for (i64 i = 1; i < vec.size(); i++)
    vec[i]->locals_offset = vec[i - 1]->locals_offset + vec[i - 1]->num_locals;

  globals_offset = last.locals_offset + last.num_locals;
  first.globals_offset = globals_offset;

  for (i64 i = 1; i < vec.size(); i++)
    vec[i]->globals_offset = vec[i - 1]->globals_offset + vec[i - 1]->num_globals;

  undefs_offset = last.globals_offset + last.num_globals;
  first.undefs_offset = undefs_offset;

  for (i64 i = 1; i < vec.size(); i++)
    vec[i]->undefs_offset = vec[i - 1]->undefs_offset + vec[i - 1]->num_undefs;

  for (i64 i = 1; i < vec.size(); i++)
    vec[i]->strtab_offset = vec[i - 1]->strtab_offset + vec[i - 1]->strtab_size;

  i64 num_symbols = last.undefs_offset + last.num_undefs;
  this->hdr.size = num_symbols * sizeof(MachSym<E>);
  ctx.strtab.hdr.size = last.strtab_offset + last.strtab_size;

  // Update symbol's output_symtab_idx
  tbb::parallel_for_each(vec, [&](InputFile<E> *file) {
    i64 locals = file->locals_offset;
    i64 globals = file->globals_offset;
    i64 undefs = file->undefs_offset;

    for (Symbol<E> *sym : file->syms) {
      if (sym && sym->file == file && sym->output_symtab_idx == -2) {
        if (sym->is_imported)
          sym->output_symtab_idx = undefs++;
        else if (sym->visibility == SCOPE_GLOBAL)
          sym->output_symtab_idx = globals++;
        else
          sym->output_symtab_idx = locals++;
      }
    }
  });
}

template <typename E>
void SymtabSection<E>::copy_buf(Context<E> &ctx) {
  // Create symbols for -add_ast_path
  MachSym<E> *buf = (MachSym<E> *)(ctx.buf + this->hdr.offset);
  u8 *strtab = ctx.buf + ctx.strtab.hdr.offset;
  i64 stroff = strtab_init_image.size();

  memcpy(strtab, strtab_init_image.data(), strtab_init_image.size());

  for (std::string_view path : ctx.arg.add_ast_path) {
    MachSym<E> &msym = *buf++;
    msym.stroff = stroff;
    msym.n_type = N_AST;
    stroff += write_string(strtab + stroff, path);
  }

  // Copy symbols from input files to an output file
  std::vector<InputFile<E> *> files;
  append(files, ctx.objs);
  append(files, ctx.dylibs);

  tbb::parallel_for_each(files, [&](InputFile<E> *file) {
    file->populate_symtab(ctx);
  });
}

template <typename E>
void IndirectSymtabSection<E>::compute_size(Context<E> &ctx) {
  ctx.got.hdr.reserved1 = 0;
  i64 n = ctx.got.syms.size();

  ctx.thread_ptrs.hdr.reserved1 = n;
  n += ctx.thread_ptrs.syms.size();

  ctx.stubs.hdr.reserved1 = n;
  n += ctx.stubs.syms.size();

  if (ctx.lazy_symbol_ptr) {
    ctx.lazy_symbol_ptr->hdr.reserved1 = n;
    n += ctx.stubs.syms.size();
  }

  this->hdr.size = n * ENTRY_SIZE;
}

template <typename E>
void IndirectSymtabSection<E>::copy_buf(Context<E> &ctx) {
  ul32 *buf = (ul32 *)(ctx.buf + this->hdr.offset);

  auto get_idx = [&](Symbol<E> &sym) -> u32 {
    if (sym.is_abs && sym.visibility != SCOPE_GLOBAL)
      return INDIRECT_SYMBOL_ABS | INDIRECT_SYMBOL_LOCAL;
    if (sym.is_abs)
      return INDIRECT_SYMBOL_ABS;
    if (sym.visibility != SCOPE_GLOBAL)
      return INDIRECT_SYMBOL_LOCAL;
    return sym.output_symtab_idx;
  };

  for (Symbol<E> *sym : ctx.got.syms)
    *buf++ = get_idx(*sym);

  for (Symbol<E> *sym : ctx.thread_ptrs.syms)
    *buf++ = get_idx(*sym);

  for (Symbol<E> *sym : ctx.stubs.syms)
    *buf++ = get_idx(*sym);

  if (ctx.lazy_symbol_ptr)
    for (Symbol<E> *sym : ctx.stubs.syms)
      *buf++ = get_idx(*sym);
}

// Create __DATA,__objc_imageinfo section contents by merging input
// __objc_imageinfo sections.
template <typename E>
std::unique_ptr<ObjcImageInfoSection<E>>
ObjcImageInfoSection<E>::create(Context<E> &ctx) {
  ObjcImageInfo info = {};

  for (ObjectFile<E> *file : ctx.objs) {
    if (!file->objc_image_info)
      continue;

    // Make sure that all object files have the same Swift version.
    ObjcImageInfo &info2 = *file->objc_image_info;
    if (info.swift_version == 0)
      info.swift_version = info2.swift_version;

    if (info.swift_version != info2.swift_version && info2.swift_version != 0)
      Error(ctx) << *file << ": incompatible __objc_imageinfo swift version"
                 << (u32)info.swift_version << " " << (u32)info2.swift_version;

    // swift_lang_version is set to the newest.
    info.swift_lang_version =
      std::max<u32>(info.swift_lang_version, info2.swift_lang_version);
  }

  // This property is on if it is on in all input object files
  auto has_category_class = [](ObjectFile<E> *file) -> bool {
    if (ObjcImageInfo *info = file->objc_image_info)
      return info->flags & OBJC_IMAGE_HAS_CATEGORY_CLASS_PROPERTIES;
    return false;
  };

  if (std::all_of(ctx.objs.begin(), ctx.objs.end(), has_category_class))
    info.flags |= OBJC_IMAGE_HAS_CATEGORY_CLASS_PROPERTIES;

  return std::make_unique<ObjcImageInfoSection<E>>(ctx, info);
}

template <typename E>
void ObjcImageInfoSection<E>::copy_buf(Context<E> &ctx) {
  memcpy(ctx.buf + this->hdr.offset, &contents, sizeof(contents));
}

// Input __mod_init_func sections contain pointers to global initializer
// functions. Since the addresses in the section are absolute, they need
// base relocations, so the scheme is not efficient in PIC (position-
// independent code).
//
// __init_offset is a new section to make it more efficient in PIC.
// The section contains 32-bit offsets from the beginning of the image
// to initializer functions. The section doesn't need base relocations.
//
// If `-init_offsets` is given, the linker converts input __mod_init_func
// sections into an __init_offset section.
template <typename E>
void InitOffsetsSection<E>::compute_size(Context<E> &ctx) {
  this->hdr.size = 0;
  for (ObjectFile<E> *file : ctx.objs)
    this->hdr.size += file->init_functions.size() * 4;
}

template <typename E>
void InitOffsetsSection<E>::copy_buf(Context<E> &ctx) {
  ul32 *buf = (ul32 *)(ctx.buf + this->hdr.offset);

  for (ObjectFile<E> *file : ctx.objs)
    for (Symbol<E> *sym : file->init_functions)
      *buf++ = sym->get_addr(ctx) - ctx.mach_hdr.hdr.addr;
}

template <typename E>
void CodeSignatureSection<E>::compute_size(Context<E> &ctx) {
  std::string filename = filepath(ctx.arg.final_output).filename().string();
  i64 filename_size = align_to(filename.size() + 1, 16);
  i64 num_blocks = align_to(this->hdr.offset, E::page_size) / E::page_size;
  this->hdr.size = sizeof(CodeSignatureHeader) + sizeof(CodeSignatureBlobIndex) +
                   sizeof(CodeSignatureDirectory) + filename_size +
                   num_blocks * SHA256_SIZE;
}

// A __code_signature section contains a digital signature for an output
// file so that the system can identify who created it.
//
// On ARM macOS, __code_signature is mandatory even if we don't have a key
// to sign. The signature we append in the following function is just
// SHA256 hashes of each page. Such signature is called the "ad-hoc"
// signature. Although mandating the ad-hoc signature doesn't make much
// sense because anyone can compute it, we need to always create it
// because otherwise the loader will simply rejects our output.
//
// On x86-64 macOS, __code_signature is optional. The loader doesn't reject
// an executable with no signature section.
template <typename E>
void CodeSignatureSection<E>::write_signature(Context<E> &ctx) {
  Timer t(ctx, "write_signature");

  u8 *buf = ctx.buf + this->hdr.offset;
  memset(buf, 0, this->hdr.size);

  std::string filename = filepath(ctx.arg.final_output).filename().string();
  i64 filename_size = align_to(filename.size() + 1, 16);
  i64 num_blocks = align_to(this->hdr.offset, E::page_size) / E::page_size;

  // Fill code-sign header fields
  CodeSignatureHeader &sighdr = *(CodeSignatureHeader *)buf;
  buf += sizeof(sighdr);

  sighdr.magic = CSMAGIC_EMBEDDED_SIGNATURE;
  sighdr.length = this->hdr.size;
  sighdr.count = 1;

  CodeSignatureBlobIndex &idx = *(CodeSignatureBlobIndex *)buf;
  buf += sizeof(idx);

  idx.type = CSSLOT_CODEDIRECTORY;
  idx.offset = sizeof(sighdr) + sizeof(idx);

  CodeSignatureDirectory &dir = *(CodeSignatureDirectory *)buf;
  buf += sizeof(dir);

  dir.magic = CSMAGIC_CODEDIRECTORY;
  dir.length = sizeof(dir) + filename_size + num_blocks * SHA256_SIZE;
  dir.version = CS_SUPPORTSEXECSEG;
  dir.flags = CS_ADHOC | CS_LINKER_SIGNED;
  dir.hash_offset = sizeof(dir) + filename_size;
  dir.ident_offset = sizeof(dir);
  dir.n_code_slots = num_blocks;
  dir.code_limit = this->hdr.offset;
  dir.hash_size = SHA256_SIZE;
  dir.hash_type = CS_HASHTYPE_SHA256;
  dir.page_size = std::countr_zero(E::page_size);
  dir.exec_seg_base = ctx.text_seg->cmd.fileoff;
  dir.exec_seg_limit = ctx.text_seg->cmd.filesize;
  if (ctx.output_type == MH_EXECUTE)
    dir.exec_seg_flags = CS_EXECSEG_MAIN_BINARY;

  memcpy(buf, filename.data(), filename.size());
  buf += filename_size;

  // Compute a hash value for each block.
  auto compute_hash = [&](i64 i) {
    u8 *start = ctx.buf + i * E::page_size;
    u8 *end = ctx.buf + std::min<i64>((i + 1) * E::page_size, this->hdr.offset);
    sha256_hash(start, end - start, buf + i * SHA256_SIZE);
  };

  for (i64 i = 0; i < num_blocks; i += 1024) {
    i64 j = std::min(num_blocks, i + 1024);
    tbb::parallel_for(i, j, compute_hash);

#if __APPLE__
    // Calling msync() with MS_ASYNC speeds up the following msync()
    // with MS_INVALIDATE.
    if (ctx.output_file->is_mmapped)
      msync(ctx.buf + i * E::page_size, 1024 * E::page_size, MS_ASYNC);
#endif
  }

  // A LC_UUID load command may also contain a crypto hash of the
  // entire file. We compute its value as a tree hash.
  if (ctx.arg.uuid == UUID_HASH) {
    u8 uuid[SHA256_SIZE];
    sha256_hash(ctx.buf + this->hdr.offset, this->hdr.size, uuid);

    // Indicate that this is UUIDv4 as defined by RFC4122.
    uuid[6] = (uuid[6] & 0b00001111) | 0b01010000;
    uuid[8] = (uuid[8] & 0b00111111) | 0b10000000;

    memcpy(ctx.uuid, uuid, 16);

    // Rewrite the load commands to write the updated UUID and
    // recompute code signatures for the updated blocks.
    ctx.mach_hdr.copy_buf(ctx);

    for (i64 i = 0; i * E::page_size < ctx.mach_hdr.hdr.size; i++)
      compute_hash(i);
  }

#if __APPLE__
  // If an executable's pages have been created via an mmap(), the output
  // file will fail for the code signature verification because the macOS
  // kernel wrongly assume that the pages may be mutable after the code
  // verification, though it is actually impossible after munmap().
  //
  // In order to workaround the issue, we call msync() to invalidate all
  // mmapped pages.
  //
  // https://openradar.appspot.com/FB8914231
  if (ctx.output_file->is_mmapped) {
    Timer t2(ctx, "msync", &t);
    msync(ctx.buf, ctx.output_file->filesize, MS_INVALIDATE);
  }
#endif
}

template <typename E>
void DataInCodeSection<E>::compute_size(Context<E> &ctx) {
  assert(contents.empty());

  for (ObjectFile<E> *file : ctx.objs) {
    LinkEditDataCommand *cmd =
      (LinkEditDataCommand *)file->find_load_command(ctx, LC_DATA_IN_CODE);
    if (!cmd)
      continue;

    std::span<DataInCodeEntry> entries = {
      (DataInCodeEntry *)(file->mf->data + cmd->dataoff),
      cmd->datasize / sizeof(DataInCodeEntry),
    };

    for (Subsection<E> *subsec : file->subsections) {
      if (entries.empty())
        break;

      DataInCodeEntry &ent = entries[0];
      if (subsec->input_addr + subsec->input_size < ent.offset)
        continue;

      if (ent.offset < subsec->input_addr + subsec->input_size) {
        u32 offset = subsec->get_addr(ctx) + subsec->input_addr - ent.offset -
                     ctx.text_seg->cmd.vmaddr;
        contents.push_back({offset, ent.length, ent.kind});
      }

      entries = entries.subspan(1);
    }
  }

  this->hdr.size = contents.size() * sizeof(contents[0]);
}

template <typename E>
void DataInCodeSection<E>::copy_buf(Context<E> &ctx) {
  write_vector(ctx.buf + this->hdr.offset, contents);
}

// Collect all locations that needs fixing on page-in
template <typename E>
std::vector<Fixup<E>> get_fixups(Context<E> &ctx) {
  std::vector<std::vector<Fixup<E>>> vec(ctx.objs.size());

  tbb::parallel_for((i64)0, (i64)ctx.objs.size(), [&](i64 i) {
    for (Subsection<E> *subsec : ctx.objs[i]->subsections) {
      if (!subsec->is_alive)
        continue;

      for (Relocation<E> &r : subsec->get_rels()) {
        if (r.type == E::abs_rel && r.sym() && r.sym()->is_imported)
          vec[i].push_back({subsec->get_addr(ctx) + r.offset, r.sym(),
                            (u64)r.addend});
        else if (needs_rebasing(r))
          vec[i].push_back({subsec->get_addr(ctx) + r.offset});
      }
    }
  });

  std::vector<Fixup<E>> fixups = flatten(vec);

  for (Symbol<E> *sym : ctx.got.syms)
    fixups.push_back({sym->get_got_addr(ctx), sym->is_imported ? sym : nullptr});

  for (Symbol<E> *sym : ctx.thread_ptrs.syms)
    fixups.push_back({sym->get_tlv_addr(ctx), sym->is_imported ? sym : nullptr});

  tbb::parallel_sort(fixups, [](const Fixup<E> &a, const Fixup<E> &b) {
    return a.addr < b.addr;
  });
  return fixups;
}

// Returns fixups for a given segment
template <typename E>
static std::span<Fixup<E>>
get_segment_fixups(std::vector<Fixup<E>> &fixups, OutputSegment<E> &seg) {
  auto begin = std::partition_point(fixups.begin(), fixups.end(),
                                    [&](const Fixup<E> &x) {
    return x.addr < seg.cmd.vmaddr;
  });

  if (begin == fixups.end())
    return {};

  auto end = std::partition_point(begin, fixups.end(), [&](const Fixup<E> &x) {
    return x.addr < seg.cmd.vmaddr + seg.cmd.vmsize;
  });

  return {&*begin, (size_t)(end - begin)};
}

template <typename E>
bool operator<(const SymbolAddend<E> &a, const SymbolAddend<E> &b) {
  if (a.sym != b.sym)
    return *a.sym < *b.sym;
  return a.addend < b.addend;
}

// A chained fixup can contain an addend if its value is equal to or
// smaller than 255.
static constexpr i64 MAX_INLINE_ADDEND = 255;

template <typename E>
static std::tuple<std::vector<SymbolAddend<E>>, u32>
get_dynsyms(std::vector<Fixup<E>> &fixups) {
  // Collect all dynamic relocations and sort them by addend
  std::vector<SymbolAddend<E>> syms;
  for (Fixup<E> &x : fixups)
    if (x.sym)
      syms.push_back({x.sym, x.addend <= MAX_INLINE_ADDEND ? 0 : x.addend});

  sort(syms);
  remove_duplicates(syms);

  // Set symbol ordinal
  for (i64 i = syms.size() - 1; i >= 0; i--)
    syms[i].sym->fixup_ordinal = i;

  // Dynamic relocations can have an arbitrary large addend. For example,
  // if you initialize a global variable pointer as `int *p = foo + (1<<31)`
  // and `foo` is an imported symbol, it generates a dynamic relocation with
  // an addend of 1<<31. Such large addend don't fit in a 64-bit in-place
  // chained relocation, as its `addend` bitfield is only 8 bit wide.
  //
  // A dynamic relocation with large addend is represented as a pair of
  // symbol name and an addend in the import table. We use an import table
  // structure with an addend field only if it's necessary.
  u64 max = 0;
  for (SymbolAddend<E> &x : syms)
    max = std::max(max, x.addend);

  u32 format;
  if (max == 0)
    format = DYLD_CHAINED_IMPORT;
  else if ((u32)max == max)
    format = DYLD_CHAINED_IMPORT_ADDEND;
  else
    format = DYLD_CHAINED_IMPORT_ADDEND64;

  return {std::move(syms), format};
}

template <typename E>
u8 *ChainedFixupsSection<E>::allocate(i64 size) {
  i64 off = contents.size();
  contents.resize(contents.size() + size);
  return contents.data() + off;
}

// macOS 13 or later supports a new mechanism to apply dynamic relocations.
// In this comment, I'll explain what it is and how it works.
//
// In the traditional dynamic linking mechanism, data relocations are
// applied eagerly on process startup; only function symbols are resolved
// lazily through PLT/stubs. The new mechanism called the "page-in linking"
// changes that; with the page-in linking, the kernel now applies data
// relocations as it loads a new page from disk to memory. The page-in
// linking has the following benefits compared to the ttraditional model:
//
//  1. Data relocations are no longer applied eagerly on startup, shotening
//     the process startup time.
//
//  2. It reduces the number of dirty pages because until a process actually
//     access a data page, no page is loaded to memory. Moreover, the kernel
//     can now discard (instead of page out) an read-only page with
//     relocations under memory pressure because it knows how to reconstruct
//     the same page by applying the same dynamic relocations.
//
// `__chainfixups` section contains data needed for the page-in linking.
// The section contains the first-level page table. Specifically, we have
// one DyldChainedStartsInSegment data structure for each segment, and the
// data structure contains a u16 array of the same length as the number of
// pages in the segment. Each u16 value represents an in-page offset within
// the corresponding page of the first location that needs fixing. With that
// information, the kernel is able to know the first dynamically-linked
// location in a page when it pages in a new page from disk.
//
// Unlike the traiditional dynamic linking model, there's no separate
// relocation table. Relocation record is represented in a compact 64-bit
// encoding and directly embedded to the place where the relocation is
// applied to. In addition to that, each in-place relocation record contains
// an offset to the next location in the same page that needs fixing. With
// that, the kernel is able to follow that chain to apply all relocations
// for a given page.
//
// This mechanism is so-called the "chained fixups", as the in-place
// relocations form a linked list.
template <typename E>
void ChainedFixupsSection<E>::compute_size(Context<E> &ctx) {
  fixups = get_fixups(ctx);
  if (fixups.empty())
    return;

  // Section header
  i64 hdr_size = align_to(sizeof(DyldChainedFixupsHeader), 8);
  auto *h = (DyldChainedFixupsHeader *)allocate(hdr_size);
  h->fixups_version = 0;
  h->starts_offset = contents.size();

  // Segment header
  i64 seg_count = ctx.segments.back()->seg_idx + 1;
  i64 starts_offset = contents.size();
  i64 starts_size = align_to(sizeof(DyldChainedStartsInImage) +
                             seg_count * 4, 8);
  auto *starts = (DyldChainedStartsInImage *)allocate(starts_size);
  starts->seg_count = seg_count;

  // Write the first-level page table for each segment
  for (std::unique_ptr<OutputSegment<E>> &seg : ctx.segments) {
    std::span<Fixup<E>> fx = get_segment_fixups(fixups, *seg);
    if (fx.empty())
      continue;

    starts = (DyldChainedStartsInImage *)(contents.data() + starts_offset);
    starts->seg_info_offset[seg->seg_idx] = contents.size() - starts_offset;

    i64 npages =
      align_to(fx.back().addr + 1 - seg->cmd.vmaddr, E::page_size) / E::page_size;
    i64 size = align_to(sizeof(DyldChainedStartsInSegment) + npages * 2, 8);

    auto *rec = (DyldChainedStartsInSegment *)allocate(size);
    rec->size = size;
    rec->page_size = E::page_size;
    rec->pointer_format = DYLD_CHAINED_PTR_64;
    rec->segment_offset = seg->cmd.vmaddr - ctx.mach_hdr.hdr.addr;
    rec->max_valid_pointer = 0;
    rec->page_count = npages;

    for (i64 i = 0, j = 0; i < npages; i++) {
      u64 addr = seg->cmd.vmaddr + i * E::page_size;
      while (j < fixups.size() && fixups[j].addr < addr)
        j++;

      if (j < fixups.size() && fixups[j].addr < addr + E::page_size)
        rec->page_start[i] = fixups[j].addr & (E::page_size - 1);
      else
        rec->page_start[i] = DYLD_CHAINED_PTR_START_NONE;
    }
  }

  // Write symbol import table
  u32 import_format;
  std::tie(dynsyms, import_format) = get_dynsyms(fixups);

  h = (DyldChainedFixupsHeader *)contents.data();
  h->imports_count = dynsyms.size();
  h->imports_format = import_format;
  h->imports_offset = contents.size();

  if (import_format == DYLD_CHAINED_IMPORT)
    write_imports<DyldChainedImport>(ctx);
  else if (import_format == DYLD_CHAINED_IMPORT_ADDEND)
    write_imports<DyldChainedImportAddend>(ctx);
  else
    write_imports<DyldChainedImportAddend64>(ctx);

  // Write symbol names
  h = (DyldChainedFixupsHeader *)contents.data();
  h->symbols_offset = contents.size();
  h->symbols_format = 0;

  for (i64 i = 0; i < dynsyms.size(); i++)
    if (Symbol<E> *sym = dynsyms[i].sym;
        i == 0 || dynsyms[i - 1].sym != sym)
      write_string(allocate(sym->name.size() + 1), sym->name);

  contents.resize(align_to(contents.size(), 8));
  this->hdr.size = contents.size();
}

// This function is a part of ChainedFixupsSection<E>::compute_size(),
// but it's factored out as a separate function so that it can take
// a type parameter.
template <typename E>
template <typename T>
void ChainedFixupsSection<E>::write_imports(Context<E> &ctx) {
  T *imports = (T *)allocate(sizeof(T) * dynsyms.size());
  i64 nameoff = 0;

  for (i64 i = 0; i < dynsyms.size(); i++) {
    Symbol<E> &sym = *dynsyms[i].sym;

    if (ctx.arg.flat_namespace)
      imports[i].lib_ordinal = BIND_SPECIAL_DYLIB_FLAT_LOOKUP;
    else if (sym.file->is_dylib)
      imports[i].lib_ordinal = ((DylibFile<E> *)sym.file)->dylib_idx;
    else
      imports[i].lib_ordinal = BIND_SPECIAL_DYLIB_WEAK_LOOKUP;

    imports[i].weak_import = sym.is_weak;
    imports[i].name_offset = nameoff;

    if constexpr (requires (T x) { x.addend; })
      imports[i].addend = dynsyms[i].addend;

    if (i + 1 == dynsyms.size() || dynsyms[i + 1].sym != &sym)
      nameoff += sym.name.size() + 1;
  }
}

template <typename E>
void ChainedFixupsSection<E>::copy_buf(Context<E> &ctx) {
  memcpy(ctx.buf + this->hdr.offset, contents.data(), contents.size());
}

template <typename E>
static InputSection<E> &
find_input_section(Context<E> &ctx, OutputSegment<E> &seg, u64 addr) {
  auto it = std::partition_point(seg.chunks.begin(), seg.chunks.end(),
                                 [&](Chunk<E> *chunk) {
    return chunk->hdr.addr < addr;
  });

  assert(it != seg.chunks.begin());

  OutputSection<E> *osec = it[-1]->to_osec();
  assert(osec);

  auto it2 = std::partition_point(osec->members.begin(), osec->members.end(),
                                  [&](Subsection<E> *subsec) {
    return subsec->get_addr(ctx) < addr;
  });

  assert(it2 != osec->members.begin());
  return *it2[-1]->isec;
}

  // review
// This function is called after copy_sections_to_output_file().
template <typename E>
void ChainedFixupsSection<E>::write_fixup_chains(Context<E> &ctx) {
  Timer t(ctx, "write_fixup_chains");

  auto page = [](u64 addr) { return addr & ~((u64)E::page_size - 1); };

  auto get_ordinal = [&](i64 i, u64 addend) {
    for (; i < dynsyms.size(); i++)
      if (dynsyms[i].addend == addend)
        return i;
    unreachable();
  };

  for (std::unique_ptr<OutputSegment<E>> &seg : ctx.segments) {
    std::span<Fixup<E>> fx = get_segment_fixups(fixups, *seg);

    for (i64 i = 0; i < fx.size(); i++) {
      constexpr u32 stride = 4;

      u32 next = 0;
      if (i + 1 < fx.size() && page(fx[i + 1].addr) == page(fx[i].addr))
        next = (fx[i + 1].addr - fx[i].addr) / stride;

      u8 *loc = ctx.buf + seg->cmd.fileoff + (fx[i].addr - seg->cmd.vmaddr);

      if (Symbol<E> *sym = fx[i].sym) {
        if (fx[i].addr % stride)
          Error(ctx) << find_input_section(ctx, *seg, fx[i].addr)
                     << ": unaligned relocation against `" << *sym
                     << "; re-link with -no_fixup_chains";

        DyldChainedPtr64Bind *rec = (DyldChainedPtr64Bind *)loc;

        if (fx[i].addend <= MAX_INLINE_ADDEND) {
          rec->ordinal = sym->fixup_ordinal;
          rec->addend = fx[i].addend;
        } else {
          rec->ordinal = get_ordinal(sym->fixup_ordinal, fx[i].addend);
          rec->addend = 0;
        }

        rec->reserved = 0;
        rec->next = next;
        rec->bind = 1;
      } else {
        if (fx[i].addr % stride)
          Error(ctx) << find_input_section(ctx, *seg, fx[i].addr)
                     << ": unaligned base relocation; "
                     << "re-link with -no_fixup_chains";

        u64 val = *(ul64 *)loc;
        if (val & 0x00ff'fff0'0000'0000)
          Error(ctx) << seg->cmd.get_segname()
                     << ": rebase addend too large; re-link with -no_fixup_chains";

        DyldChainedPtr64Rebase *rec = (DyldChainedPtr64Rebase *)loc;
        rec->target = val;
        rec->high8 = val >> 56;
        rec->reserved = 0;
        rec->next = next;
        rec->bind = 0;
      }
    }
  }
}

template <typename E>
void StubsSection<E>::add(Context<E> &ctx, Symbol<E> *sym) {
  assert(sym->stub_idx == -1);
  sym->stub_idx = syms.size();

  syms.push_back(sym);
  this->hdr.size = syms.size() * E::stub_size;

  if (ctx.stub_helper) {
    if (ctx.stub_helper->hdr.size == 0)
      ctx.stub_helper->hdr.size = E::stub_helper_hdr_size;

    ctx.stub_helper->hdr.size += E::stub_helper_size;
    ctx.lazy_symbol_ptr->hdr.size += sizeof(Word<E>);
  }
}

template <typename E>
std::vector<u8>
encode_unwind_info(Context<E> &ctx, std::vector<Symbol<E> *> personalities,
                   std::vector<std::vector<UnwindRecord<E> *>> &pages,
                   i64 num_lsda) {
  // Compute the size of the buffer.
  i64 size = sizeof(UnwindSectionHeader) +
             personalities.size() * 4 +
             sizeof(UnwindFirstLevelPage) * (pages.size() + 1) +
             sizeof(UnwindSecondLevelPage) * pages.size() +
             sizeof(UnwindLsdaEntry) * num_lsda;

  for (std::span<UnwindRecord<E> *> span : pages)
    size += (sizeof(UnwindPageEntry) + 4) * span.size();

  // Allocate an output buffer.
  std::vector<u8> buf(size);

  // Write the section header.
  UnwindSectionHeader &uhdr = *(UnwindSectionHeader *)buf.data();
  uhdr.version = UNWIND_SECTION_VERSION;
  uhdr.encoding_offset = sizeof(uhdr);
  uhdr.encoding_count = 0;
  uhdr.personality_offset = sizeof(uhdr);
  uhdr.personality_count = personalities.size();
  uhdr.page_offset = sizeof(uhdr) + personalities.size() * 4;
  uhdr.page_count = pages.size() + 1;

  // Write the personalities
  ul32 *per = (ul32 *)(buf.data() + sizeof(uhdr));
  for (Symbol<E> *sym : personalities) {
    if (sym->file)
      *per++ = sym->get_got_addr(ctx);
    else
      Error(ctx) << "undefined symbol: " << *sym;
  }

  // Write first level pages, LSDA and second level pages
  UnwindFirstLevelPage *page1 = (UnwindFirstLevelPage *)per;
  UnwindLsdaEntry *lsda = (UnwindLsdaEntry *)(page1 + (pages.size() + 1));
  UnwindSecondLevelPage *page2 = (UnwindSecondLevelPage *)(lsda + num_lsda);

  for (std::span<UnwindRecord<E> *> span : pages) {
    page1->func_addr = span[0]->get_func_addr(ctx);
    page1->page_offset = (u8 *)page2 - buf.data();
    page1->lsda_offset = (u8 *)lsda - buf.data();

    for (UnwindRecord<E> *rec : span) {
      if (rec->lsda) {
        lsda->func_addr = rec->get_func_addr(ctx) - ctx.mach_hdr.hdr.addr;
        lsda->lsda_addr = rec->lsda->get_addr(ctx) + rec->lsda_offset -
                          ctx.mach_hdr.hdr.addr;
        lsda++;
      }
    }

    std::unordered_map<u32, u32> map;
    for (UnwindRecord<E> *rec : span)
      map.insert({rec->encoding, map.size()});

    page2->kind = UNWIND_SECOND_LEVEL_COMPRESSED;
    page2->page_offset = sizeof(UnwindSecondLevelPage);
    page2->page_count = span.size();

    UnwindPageEntry *entry = (UnwindPageEntry *)(page2 + 1);
    for (UnwindRecord<E> *rec : span) {
      entry->func_addr = rec->get_func_addr(ctx) - page1->func_addr;
      entry->encoding = map[rec->encoding];
      entry++;
    }

    page2->encoding_offset = (u8 *)entry - (u8 *)page2;
    page2->encoding_count = map.size();

    ul32 *encoding = (ul32 *)entry;
    for (std::pair<u32, u32> kv : map)
      encoding[kv.second] = kv.first;

    page1++;
    page2 = (UnwindSecondLevelPage *)(encoding + map.size());
  }

  // Write a terminator
  UnwindRecord<E> &last = *pages.back().back();
  page1->func_addr = last.subsec->get_addr(ctx) + last.subsec->input_size + 1;
  page1->page_offset = 0;
  page1->lsda_offset = (u8 *)lsda - buf.data();

  assert((u8 *)page2 <= buf.data() + buf.size());
  buf.resize((u8 *)page2 - buf.data());
  return buf;
}

// If two unwind records covers adjascent functions and have identical
// contents (i.e. have the same encoding, the same personality function
// and don't have LSDA), we can merge the two.
template <typename E>
static std::span<UnwindRecord<E> *>
merge_unwind_records(Context<E> &ctx, std::vector<UnwindRecord<E> *> &records) {
  auto can_merge = [&](UnwindRecord<E> &a, UnwindRecord<E> &b) {
    // As a special case, we don't merge unwind records with STACK_IND
    // encoding even if their encodings look the same. It is because the
    // real encoding for that record type is encoded in the instruction
    // stream and therefore the real encodings might be different.
    if constexpr (is_x86<E>)
      if ((a.encoding & UNWIND_MODE_MASK) == UNWIND_X86_64_MODE_STACK_IND ||
          (b.encoding & UNWIND_MODE_MASK) == UNWIND_X86_64_MODE_STACK_IND)
        return false;

    return a.get_func_addr(ctx) + a.code_len == b.get_func_addr(ctx) &&
           a.encoding == b.encoding &&
           a.personality == b.personality &&
           a.fde == b.fde &&
           !a.lsda && !b.lsda;
  };

  assert(!records.empty());

  i64 i = 0;
  for (i64 j = 1; j < records.size(); j++) {
    if (can_merge(*records[i], *records[j]))
      records[i]->code_len += records[j]->code_len;
    else
      records[++i] = records[j];
  }
  return {&records[0], (size_t)(i + 1)};
}

// __unwind_info stores unwind records in two-level tables. The first-level
// table specifies the upper 12 bits of function addresses. The second-level
// page entries are 32-bits long and specifies the lower 24 bits of fucntion
// addresses along with indices to personality functions.
//
// This function splits a vector of unwind records into groups so that
// records in the same group share the first-level page table.
template <typename E>
static std::vector<std::vector<UnwindRecord<E> *>>
split_records(Context<E> &ctx, std::span<UnwindRecord<E> *> records) {
  constexpr i64 max_group_size = 200;
  std::vector<std::vector<UnwindRecord<E> *>> vec;

  while (!records.empty()) {
    u64 end_addr = records[0]->get_func_addr(ctx) + (1 << 24);
    i64 i = 1;
    while (i < records.size() && i < max_group_size &&
           records[i]->get_func_addr(ctx) < end_addr)
      i++;
    vec.push_back({records.begin(), records.begin() + i});
    records = records.subspan(i);
  }
  return vec;
}

template <typename E>
void UnwindInfoSection<E>::compute_size(Context<E> &ctx) {
  std::vector<UnwindRecord<E> *> records;

  for (std::unique_ptr<OutputSegment<E>> &seg : ctx.segments)
    for (Chunk<E> *chunk : seg->chunks)
      if (OutputSection<E> *osec = chunk->to_osec())
        for (Subsection<E> *subsec : osec->members)
          for (UnwindRecord<E> &rec : subsec->get_unwind_records())
            records.push_back(&rec);

  if (records.empty())
    return;

  auto encode_personality = [&](Symbol<E> *sym) -> u32 {
    for (i64 i = 0; i < personalities.size(); i++)
      if (personalities[i] == sym)
        return (i + 1) << std::countr_zero((u32)UNWIND_PERSONALITY_MASK);

    if (personalities.size() == 3)
      Fatal(ctx) << "too many personality functions";

    personalities.push_back(sym);
    return personalities.size() << std::countr_zero((u32)UNWIND_PERSONALITY_MASK);
  };

  for (UnwindRecord<E> *rec : records) {
    if (rec->fde)
      rec->encoding = E::unwind_mode_dwarf | rec->fde->output_offset;
    else if (rec->personality)
      rec->encoding |= encode_personality(rec->personality);

    if (rec->lsda)
      num_lsda++;
  }

  tbb::parallel_sort(records,
                     [&](const UnwindRecord<E> *a, const UnwindRecord<E> *b) {
    return a->get_func_addr(ctx) < b->get_func_addr(ctx);
  });

  std::span<UnwindRecord<E> *> records2 = merge_unwind_records(ctx, records);
  pages = split_records(ctx, records2);

  this->hdr.size = encode_unwind_info(ctx, personalities, pages, num_lsda).size();
}

template <typename E>
void UnwindInfoSection<E>::copy_buf(Context<E> &ctx) {
  if (this->hdr.size == 0)
    return;

  std::vector<u8> vec = encode_unwind_info(ctx, personalities, pages, num_lsda);
  assert(this->hdr.size == vec.size());
  write_vector(ctx.buf + this->hdr.offset, vec);
}

template <typename E>
void GotSection<E>::add(Context<E> &ctx, Symbol<E> *sym) {
  if (sym->got_idx != -1)
    return;

  sym->got_idx = syms.size();
  syms.push_back(sym);
  this->hdr.size = syms.size() * sizeof(Word<E>);
}

template <typename E>
void GotSection<E>::copy_buf(Context<E> &ctx) {
  Word<E> *buf = (Word<E> *)(ctx.buf + this->hdr.offset);

  for (i64 i = 0; i < syms.size(); i++)
    if (!syms[i]->is_imported)
      buf[i] = syms[i]->get_addr(ctx);
}

template <typename E>
void LazySymbolPtrSection<E>::copy_buf(Context<E> &ctx) {
  Word<E> *buf = (Word<E> *)(ctx.buf + this->hdr.offset);

  for (i64 i = 0; i < ctx.stubs.syms.size(); i++)
    *buf++ = ctx.stub_helper->hdr.addr + E::stub_helper_hdr_size +
             E::stub_helper_size * i;
}

template <typename E>
void ThreadPtrsSection<E>::add(Context<E> &ctx, Symbol<E> *sym) {
  assert(sym->tlv_idx == -1);
  sym->tlv_idx = syms.size();
  syms.push_back(sym);
  this->hdr.size = syms.size() * sizeof(Word<E>);
}

template <typename E>
void ThreadPtrsSection<E>::copy_buf(Context<E> &ctx) {
  ul64 *buf = (ul64 *)(ctx.buf + this->hdr.offset);
  memset(buf, 0, this->hdr.size);

  for (i64 i = 0; i < syms.size(); i++)
    if (!syms[i]->is_imported)
      buf[i] = syms[i]->get_addr(ctx);
}

// Parse CIE augmented string
template <typename E>
void CieRecord<E>::parse(Context<E> &ctx) {
  u8 *aug = (u8 *)get_contents().data() + 9;
  if (*aug != 'z')
    return;

  u8 *data = aug + strlen((char *)aug) + 1;
  read_uleb(data); // code alignment
  read_uleb(data); // data alignment
  read_uleb(data); // return address register
  read_uleb(data); // augmentation data length

  for (i64 i = 1; aug[i]; i++) {
    switch (aug[i]) {
    case 'L': {
      if ((*data & 0xf) == DW_EH_PE_sdata4)
        lsda_size = 4;
      else if ((*data & 0xf) == DW_EH_PE_absptr)
        lsda_size = 8;
      else
        Fatal(ctx) << *file << ": __eh_frame: unknown LSDA encoding: 0x"
                   << std::hex << (u32)*data;
      data++;
      break;
    }
    case 'P':
      if (*data != (DW_EH_PE_pcrel | DW_EH_PE_indirect | DW_EH_PE_sdata4))
        Fatal(ctx) << *file << ": __eh_frame: unknown personality encoding: 0x"
                   << std::hex << (u32)*data;
      data += 5;
      break;
    case 'R':
      if ((*data & 0xf) != DW_EH_PE_absptr || !(*data & DW_EH_PE_pcrel))
        Fatal(ctx) << *file << ": __eh_frame: unknown pointer encoding: 0x"
                   << std::hex << (u32)*data;
      data++;
      break;
    default:
      Fatal(ctx) << *file << ": __eh_frame: unknown augmented string: "
                 << (char *)aug;
    }
  }
}

template <typename E>
std::string_view CieRecord<E>::get_contents() const {
  const char *data = file->mf->get_contents().data() + file->eh_frame_sec->offset +
                     input_addr - file->eh_frame_sec->addr;
  return {data, (size_t)*(ul32 *)data + 4};
}

template <typename E>
void CieRecord<E>::copy_to(Context<E> &ctx) {
  u8 *buf = ctx.buf + ctx.eh_frame.hdr.offset;

  std::string_view data = get_contents();
  memcpy(buf + output_offset, data.data(), data.size());

  if (personality) {
    if (!personality->file) {
      Error(ctx) << "undefined symbol: " << *file << ": " << *personality;
      return;
    }

    i64 offset = output_offset + personality_offset;
    *(ul32 *)(buf + offset) =
      personality->get_got_addr(ctx) - (ctx.eh_frame.hdr.addr + offset);
  }
}

template <typename E>
void FdeRecord<E>::parse(Context<E> &ctx) {
  ObjectFile<E> &file = subsec->isec->file;

  auto find_cie = [&](u32 addr) {
    for (std::unique_ptr<CieRecord<E>> &cie : file.cies)
      if (cie->input_addr == addr)
        return &*cie;
    Fatal(ctx) << file << ": cannot find a CIE for a FDE at address 0x"
               << std::hex << input_addr;
  };

  // Initialize cie
  std::string_view data = get_contents();
  i64 cie_offset = *(ul32 *)(data.data() + 4);
  cie = find_cie(input_addr + 4 - cie_offset);

  // Initialize lsda
  if (cie->lsda_size) {
    u8 *buf = (u8 *)get_contents().data();
    u8 *aug = buf + 24;
    read_uleb(aug); // skip Augmentation Data Length

    i64 offset = aug - buf;
    u64 addr = *(ul32 *)aug + input_addr + offset;

    lsda = file.find_subsection(ctx, addr);
    if (!lsda)
      Fatal(ctx) << file << ": cannot find a LSDA for a FDE at address 0x"
                 << std::hex << input_addr;
  }
}

template <typename E>
std::string_view FdeRecord<E>::get_contents() const {
  ObjectFile<E> &file = subsec->isec->file;
  const char *data = file.mf->get_contents().data() + file.eh_frame_sec->offset +
                     input_addr - file.eh_frame_sec->addr;
  return {data, (size_t)*(ul32 *)data + 4};
}

template <typename E>
void FdeRecord<E>::copy_to(Context<E> &ctx) {
  u8 *buf = ctx.buf + ctx.eh_frame.hdr.offset + output_offset;

  // Copy FDE contents
  std::string_view data = get_contents();
  memcpy(buf, data.data(), data.size());

  // Relocate CIE offset
  *(ul32 *)(buf + 4) = output_offset + 4 - cie->output_offset;

  // Relocate function start address
  u64 output_addr = ctx.eh_frame.hdr.addr + output_offset;
  *(ul64 *)(buf + 8) = (i32)(subsec->get_addr(ctx) - output_addr - 8);

  if (lsda) {
    u8 *aug = buf + 24;
    read_uleb(aug); // skip Augmentation Data Length

    i64 offset = aug - buf;
    u64 addr = *(ul32 *)aug + input_addr + offset;
    i64 val = lsda->get_addr(ctx) - output_addr - offset +
              addr - lsda->input_addr;

    if (cie->lsda_size == 4) {
      *(ul32 *)aug = val;
    } else {
      assert(cie->lsda_size == 8);
      *(ul64 *)aug = val;
    }
  }
}

template <typename E>
void EhFrameSection<E>::compute_size(Context<E> &ctx) {
  // Remove dead CIEs and FDEs
  tbb::parallel_for_each(ctx.objs, [&](ObjectFile<E> *file) {
    std::erase_if(file->fdes, [](FdeRecord<E> &fde) {
      return !fde.subsec->is_alive;
    });

    for (FdeRecord<E> &fde : file->fdes)
      fde.cie->is_alive = true;

    std::erase_if(file->cies, [](std::unique_ptr<CieRecord<E>> &cie) {
      return !cie->is_alive;
    });
  });

  i64 offset = 0;

  for (ObjectFile<E> *file : ctx.objs) {
    for (std::unique_ptr<CieRecord<E>> &cie : file->cies) {
      cie->output_offset = offset;
      offset += cie->size();
    }

    for (FdeRecord<E> &fde : file->fdes) {
      fde.output_offset = offset;
      offset += fde.size();
    }
  }

  this->hdr.size = offset;
}

template <typename E>
void EhFrameSection<E>::copy_buf(Context<E> &ctx) {
  tbb::parallel_for_each(ctx.objs, [&](ObjectFile<E> *file) {
    for (std::unique_ptr<CieRecord<E>> &cie : file->cies)
      cie->copy_to(ctx);

    for (FdeRecord<E> &fde : file->fdes)
      fde.copy_to(ctx);
  });
}

template <typename E>
SectCreateSection<E>::SectCreateSection(Context<E> &ctx, std::string_view seg,
                                        std::string_view sect,
                                        std::string_view contents)
  : Chunk<E>(ctx, seg, sect), contents(contents) {
  this->hdr.size = contents.size();
  ctx.chunk_pool.emplace_back(this);
}

template <typename E>
void SectCreateSection<E>::copy_buf(Context<E> &ctx) {
  write_string(ctx.buf + this->hdr.offset, contents);
}

using E = MOLD_TARGET;

template class OutputSegment<E>;
template class OutputMachHeader<E>;
template class OutputSection<E>;
template class RebaseSection<E>;
template class BindSection<E>;
template class LazyBindSection<E>;
template class ExportSection<E>;
template class FunctionStartsSection<E>;
template class SymtabSection<E>;
template class StrtabSection<E>;
template class IndirectSymtabSection<E>;
template class ObjcStubsSection<E>;
template class InitOffsetsSection<E>;
template class CodeSignatureSection<E>;
template class ObjcImageInfoSection<E>;
template class DataInCodeSection<E>;
template class ChainedFixupsSection<E>;
template class StubsSection<E>;
template class StubHelperSection<E>;
template class UnwindInfoSection<E>;
template class GotSection<E>;
template class LazySymbolPtrSection<E>;
template class ThreadPtrsSection<E>;
template class CieRecord<E>;
template class FdeRecord<E>;
template class EhFrameSection<E>;
template class SectCreateSection<E>;

} // namespace mold::macho


// 文件 mold.h
#pragma once

#include "macho.h"
#include "lto.h"
#include "../common/common.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <tbb/concurrent_hash_map.h>
#include <tbb/spin_mutex.h>
#include <tbb/task_group.h>
#include <tbb/task_group.h>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace mold::macho {

template <typename E> class Chunk;
template <typename E> class InputSection;
template <typename E> class ObjectFile;
template <typename E> class OutputSection;
template <typename E> class Subsection;
template <typename E> struct Context;
template <typename E> struct FdeRecord;
template <typename E> struct Symbol;

//
// input-files.cc
//

// We read Mach-O-native relocations from input files and translate
// them to mold's representation of relocations and then attach them
// to subsections.
template <typename E>
struct Relocation {
  u64 get_addr(Context<E> &ctx) const {
    return sym() ? sym()->get_addr(ctx) : subsec()->get_addr(ctx);
  }

  // A relocation refers to either a symbol or a subsection
  Symbol<E> *sym() const {
    return is_sym ? (Symbol<E> *)target : nullptr;
  }

  Subsection<E> *subsec() const {
    return is_sym ? nullptr : (Subsection<E> *)target;
  }

  bool refers_to_tls() const {
    if (sym() && sym()->subsec) {
      u32 type = sym()->subsec->isec->hdr.type;
      return type == S_THREAD_LOCAL_REGULAR || type == S_THREAD_LOCAL_ZEROFILL;
    }
    return false;
  }

  void *target = nullptr;
  i64 addend = 0;
  u32 offset = 0;
  u8 type = -1;
  u8 size = 0;
  bool is_sym : 1 = false;
  bool is_subtracted : 1 = false;

  // For range extension thunks
  i16 thunk_idx = -1;
  i16 thunk_sym_idx = -1;
};

template <typename E>
std::ostream &operator<<(std::ostream &out, const Relocation<E> &rel) {
  out << rel_to_string<E>(rel.type);
  return out;
}

// UnwindRecord represent a record describing how to handle exceptions.
// At runtime, the exception handler searches an unwinding record by
// instruction pointer.
template <typename E>
struct UnwindRecord {
  u64 get_func_addr(Context<E> &ctx) const {
    return subsec->get_addr(ctx) + input_offset;
  }

  Subsection<E> *subsec = nullptr;
  Symbol<E> *personality = nullptr;
  Subsection<E> *lsda = nullptr;
  FdeRecord<E> *fde = nullptr;
  u32 input_offset = 0;
  u32 code_len = 0;
  u32 encoding = 0;
  u32 lsda_offset = 0;
};

template <typename E>
struct CieRecord {
  void parse(Context<E> &ctx);

  std::string_view get_contents() const;
  i64 size() const { return get_contents().size(); }
  void copy_to(Context<E> &ctx);

  ObjectFile<E> *file;
  Symbol<E> *personality = nullptr;
  u32 input_addr = 0;
  u32 personality_offset = 0;
  u32 output_offset = (u32)-1;
  u8 lsda_size = 0;
  bool is_alive = false;
};

template <typename E>
struct FdeRecord {
  void parse(Context<E> &ctx);
  std::string_view get_contents() const;
  i64 size() const { return get_contents().size(); }
  void copy_to(Context<E> &ctx);

  CieRecord<E> *cie = nullptr;
  Subsection<E> *subsec = nullptr;
  Subsection<E> *lsda = nullptr;
  u32 input_addr = 0;
  u32 output_offset = (u32)-1;
};

template <typename E>
class InputFile {
public:
  virtual ~InputFile() = default;
  virtual void resolve_symbols(Context<E> &ctx) = 0;
  virtual void compute_symtab_size(Context<E> &ctx) = 0;
  virtual void populate_symtab(Context<E> &ctx) = 0;

  void clear_symbols();

  MappedFile<Context<E>> *mf = nullptr;
  std::string_view filename;
  std::vector<Symbol<E> *> syms;
  i64 priority = 0;
  Atomic<bool> is_alive = false;
  bool is_dylib = false;
  bool is_hidden = false;
  bool is_weak = false;
  std::string archive_name;

  // For SymtabSection
  i32 num_stabs = 0;
  i32 num_locals = 0;
  i32 num_globals = 0;
  i32 num_undefs = 0;
  i32 stabs_offset = 0;
  i32 locals_offset = 0;
  i32 globals_offset = 0;
  i32 undefs_offset = 0;
  i32 strtab_size = 0;
  i32 strtab_offset = 0;
  std::string oso_name;

protected:
  InputFile(MappedFile<Context<E>> *mf) : mf(mf), filename(mf->name) {}
  InputFile() : filename("<internal>") {}
};

template <typename E>
class ObjectFile : public InputFile<E> {
public:
  ObjectFile() = default;
  ObjectFile(MappedFile<Context<E>> *mf) : InputFile<E>(mf) {}

  static ObjectFile *create(Context<E> &ctx, MappedFile<Context<E>> *mf,
                            std::string archive_name);
  void parse(Context<E> &ctx);
  Subsection<E> *find_subsection(Context<E> &ctx, u32 addr);
  std::vector<std::string> get_linker_options(Context<E> &ctx);
  LoadCommand *find_load_command(Context<E> &ctx, u32 type);
  void parse_compact_unwind(Context<E> &ctx);
  void parse_eh_frame(Context<E> &ctx);
  void associate_compact_unwind(Context<E> &ctx);
  void parse_mod_init_func(Context<E> &ctx);
  void resolve_symbols(Context<E> &ctx) override;
  void compute_symtab_size(Context<E> &ctx) override;
  void populate_symtab(Context<E> &ctx) override;
  bool is_objc_object(Context<E> &ctx);
  void mark_live_objects(Context<E> &ctx,
                         std::function<void(ObjectFile<E> *)> feeder);
  void convert_common_symbols(Context<E> &ctx);
  void check_duplicate_symbols(Context<E> &ctx);
  std::string_view get_linker_optimization_hints(Context<E> &ctx);

  Relocation<E> read_reloc(Context<E> &ctx, const MachSection<E> &hdr, MachRel r);

  std::vector<std::unique_ptr<InputSection<E>>> sections;
  std::vector<Subsection<E> *> subsections;
  std::vector<Subsection<E> *> sym_to_subsec;
  std::span<MachSym<E>> mach_syms;
  std::vector<Symbol<E>> local_syms;
  std::vector<UnwindRecord<E>> unwind_records;
  std::vector<std::unique_ptr<CieRecord<E>>> cies;
  std::vector<FdeRecord<E>> fdes;
  MachSection<E> *eh_frame_sec = nullptr;
  ObjcImageInfo *objc_image_info = nullptr;
  LTOModule *lto_module = nullptr;

  // For __init_offsets
  MachSection<E> *mod_init_func = nullptr;
  std::vector<Symbol<E> *> init_functions;

  // For the internal file and LTO object files
  std::vector<MachSym<E>> mach_syms2;

  // For the internal file
  void add_msgsend_symbol(Context<E> &ctx, Symbol<E> &sym);

private:
  void parse_sections(Context<E> &ctx);
  void parse_symbols(Context<E> &ctx);
  void split_subsections_via_symbols(Context<E> &ctx);
  void init_subsections(Context<E> &ctx);
  void split_cstring_literals(Context<E> &ctx);
  void split_fixed_size_literals(Context<E> &ctx);
  void split_literal_pointers(Context<E> &ctx);
  InputSection<E> *get_common_sec(Context<E> &ctx);
  void parse_lto_symbols(Context<E> &ctx);

  // For ther internal file
  Subsection<E> *add_methname_string(Context<E> &ctx, std::string_view contents);
  Subsection<E> *add_selrefs(Context<E> &ctx, Subsection<E> &methname);

  MachSection<E> *unwind_sec = nullptr;
  std::unique_ptr<MachSection<E>> common_hdr;
  InputSection<E> *common_sec = nullptr;
  bool has_debug_info = false;

  std::vector<std::unique_ptr<Subsection<E>>> subsec_pool;
  std::vector<std::unique_ptr<MachSection<E>>> mach_sec_pool;
};

template <typename E>
class DylibFile : public InputFile<E> {
public:
  static DylibFile *create(Context<E> &ctx, MappedFile<Context<E>> *mf);

  void parse(Context<E> &ctx);
  void resolve_symbols(Context<E> &ctx) override;
  void compute_symtab_size(Context<E> &ctx) override;
  void populate_symtab(Context<E> &ctx) override;

  std::string_view install_name;
  i64 dylib_idx = 0;
  bool is_reexported = false;

  std::vector<std::string_view> reexported_libs;
  std::vector<std::string> rpaths;
  std::vector<DylibFile<E> *> hoisted_libs;

private:
  DylibFile(Context<E> &ctx, MappedFile<Context<E>> *mf);

  void parse_tapi(Context<E> &ctx);
  void parse_dylib(Context<E> &ctx);
  void add_export(Context<E> &ctx, std::string_view name, u32 flags);
  void read_trie(Context<E> &ctx, u8 *start, i64 offset, const std::string &prefix);

  std::map<std::string_view, u32> exports;
};

template <typename E>
std::ostream &operator<<(std::ostream &out, const InputFile<E> &file);

//
// input-sections.cc
//

// InputSection represents an input section in an input file. InputSection
// is always split into one or more Subsections. If an input section is not
// splittable, we still create a subsection and let it cover the entire input
// section.
template <typename E>
class InputSection {
public:
  InputSection(Context<E> &ctx, ObjectFile<E> &file, const MachSection<E> &hdr,
               u32 secidx);
  void parse_relocations(Context<E> &ctx);

  ObjectFile<E> &file;
  const MachSection<E> &hdr;
  u32 secidx = 0;
  OutputSection<E> &osec;
  std::string_view contents;
  std::vector<Symbol<E> *> syms;
  std::vector<Relocation<E>> rels;
};

template <typename E>
std::ostream &operator<<(std::ostream &out, const InputSection<E> &sec);

// Subsection represents a region in an InputSection.
template <typename E>
class Subsection {
public:
  u64 get_addr(Context<E> &ctx) const {
    return isec->osec.hdr.addr + output_offset;
  }

  std::string_view get_contents() {
    assert(isec->hdr.type != S_ZEROFILL);
    return isec->contents.substr(input_addr - isec->hdr.addr, input_size);
  }

  std::span<UnwindRecord<E>> get_unwind_records() {
    return std::span<UnwindRecord<E>>(isec->file.unwind_records)
      .subspan(unwind_offset, nunwind);
  }

  std::span<Relocation<E>> get_rels() const {
    return std::span<Relocation<E>>(isec->rels).subspan(rel_offset, nrels);
  }

  void scan_relocations(Context<E> &ctx);
  void apply_reloc(Context<E> &ctx, u8 *buf);

  union {
    InputSection<E> *isec = nullptr;
    Subsection<E> *replacer; // used only if `is_replaced` is true
  };

  u32 input_addr = 0;
  u32 input_size = 0;
  u32 output_offset = (u32)-1;
  u32 rel_offset = 0;
  u32 nrels = 0;
  u32 unwind_offset = 0;
  u32 nunwind = 0;

  Atomic<u8> p2align = 0;
  Atomic<bool> is_alive = true;
  bool added_to_osec : 1 = false;
  bool is_replaced : 1 = false;
  bool has_compact_unwind : 1 = false;
};

template <typename E>
std::vector<Relocation<E>>
read_relocations(Context<E> &ctx, ObjectFile<E> &file, const MachSection<E> &hdr);

//
// Symbol
//

enum {
  NEEDS_GOT              = 1 << 0,
  NEEDS_STUB             = 1 << 1,
  NEEDS_THREAD_PTR       = 1 << 2,
  NEEDS_RANGE_EXTN_THUNK = 1 << 3,
};

enum {
  SCOPE_LOCAL,  // input file visibility
  SCOPE_MODULE, // output file visibility (non-exported symbol)
  SCOPE_GLOBAL, // global visibility (exported symbol)
};

template <typename E>
struct Symbol {
  Symbol() = default;
  Symbol(std::string_view name) : name(name) {}
  Symbol(const Symbol<E> &other) : name(other.name) {}

  u64 get_addr(Context<E> &ctx) const {
    if (stub_idx != -1)
      return ctx.stubs.hdr.addr + stub_idx * E::stub_size;
    if (subsec) {
      assert(subsec->is_alive);
      return subsec->get_addr(ctx) + value;
    }
    return value;
  }

  u64 get_got_addr(Context<E> &ctx) const {
    assert(got_idx != -1);
    return ctx.got.hdr.addr + got_idx * sizeof(Word<E>);
  }

  u64 get_tlv_addr(Context<E> &ctx) const {
    assert(tlv_idx != -1);
    return ctx.thread_ptrs.hdr.addr + tlv_idx * sizeof(Word<E>);
  }

  bool has_stub() const { return stub_idx != -1; }
  bool has_got() const { return got_idx != -1; }
  bool has_tlv() const { return tlv_idx != -1; }

  std::string_view name;
  InputFile<E> *file = nullptr;
  Subsection<E> *subsec = nullptr;
  u64 value = 0;

  i32 stub_idx = -1;
  i32 got_idx = -1;
  i32 tlv_idx = -1;

  tbb::spin_mutex mu;

  Atomic<u8> flags = 0;

  u8 visibility : 2 = SCOPE_LOCAL;
  bool is_common : 1 = false;
  bool is_weak : 1 = false;
  bool is_abs : 1 = false;
  bool is_tlv : 1 = false;
  bool no_dead_strip : 1 = false;

  // `is_exported` is true if this symbol is exported from this Mach-O
  // file. `is_imported` is true if this symbol is resolved by the dynamic
  // loader at load time.
  //
  // Note that if `-flat_namespace` is given (which is rare), both are
  // true. In that case, dynamic symbols are resolved in the same
  // semantics as ELF's.
  bool is_imported : 1 = false;
  bool is_exported : 1 = false;

  union {
    // For range extension thunks. Used by OutputSection::compute_size().
    struct {
      i16 thunk_idx = -1;
      i16 thunk_sym_idx = -1;
    };

    // For chained fixups. Used by ChainedFixupsSection.
    i32 fixup_ordinal;
  };

  // For symtab
  i32 output_symtab_idx = -1;
};

template <typename E>
std::ostream &operator<<(std::ostream &out, const Symbol<E> &sym);

// This operator defines a total order over symbols. This is used to
// make the output deterministic.
template <typename E>
inline bool operator<(const Symbol<E> &a, const Symbol<E> &b) {
  return std::tuple{a.file->priority, a.value} <
         std::tuple{b.file->priority, b.value};
}

//
// output-chunks.cc
//

template <typename E>
class OutputSegment {
public:
  static OutputSegment<E> *
  get_instance(Context<E> &ctx, std::string_view name);

  void set_offset(Context<E> &ctx, i64 fileoff, u64 vmaddr);

  SegmentCommand<E> cmd = {};
  i32 seg_idx = -1;
  std::vector<Chunk<E> *> chunks;

private:
  void set_offset_regular(Context<E> &ctx, i64 fileoff, u64 vmaddr);
  void set_offset_linkedit(Context<E> &ctx, i64 fileoff, u64 vmaddr);

  OutputSegment(std::string_view name);
};

template <typename E>
class Chunk {
public:
  Chunk(Context<E> &ctx, std::string_view segname, std::string_view sectname) {
    ctx.chunks.push_back(this);
    hdr.set_segname(segname);
    hdr.set_sectname(sectname);
    seg = OutputSegment<E>::get_instance(ctx, segname);
  }

  virtual ~Chunk() = default;
  virtual void compute_size(Context<E> &ctx) {}
  virtual void copy_buf(Context<E> &ctx) {}

  OutputSection<E> *to_osec() const  {
    if (is_output_section)
      return (OutputSection<E> *)this;
    return nullptr;
  }

  MachSection<E> hdr = {};
  u32 sect_idx = 0;
  bool is_hidden = false;
  OutputSegment<E> *seg = nullptr;

protected:
  bool is_output_section = false;
};

template <typename E>
std::ostream &operator<<(std::ostream &out, const Chunk<E> &chunk);

template <typename E>
class OutputMachHeader : public Chunk<E> {
public:
  OutputMachHeader(Context<E> &ctx)
    : Chunk<E>(ctx, "__TEXT", "__mach_header") {
    this->is_hidden = true;
  }

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;
};

template <typename E>
class RangeExtensionThunk {};

template <typename E> requires is_arm<E>
class RangeExtensionThunk<E> {
public:
  RangeExtensionThunk(OutputSection<E> &osec, i64 thunk_idx, i64 offset)
    : output_section(osec), thunk_idx(thunk_idx), offset(offset) {}

  i64 size() const {
    return symbols.size() * ENTRY_SIZE;
  }

  u64 get_addr(i64 idx) const {
    return output_section.hdr.addr + offset + idx * ENTRY_SIZE;
  }

  void copy_buf(Context<E> &ctx);

  static constexpr i64 ALIGNMENT = 16;
  static constexpr i64 ENTRY_SIZE = 12;

  OutputSection<E> &output_section;
  i64 thunk_idx;
  i64 offset;
  std::mutex mu;
  std::vector<Symbol<E> *> symbols;
};

template <typename E>
class OutputSection : public Chunk<E> {
public:
  static OutputSection<E> *
  get_instance(Context<E> &ctx, std::string_view segname,
               std::string_view sectname);

  OutputSection(Context<E> &ctx, std::string_view segname,
                std::string_view sectname)
    : Chunk<E>(ctx, segname, sectname) {
    this->is_output_section = true;
  }

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;

  void add_subsec(Subsection<E> *subsec) {
    members.push_back(subsec);
    this->hdr.p2align = std::max<u32>(this->hdr.p2align, subsec->p2align);
    this->hdr.attr |= subsec->isec->hdr.attr;
    this->hdr.type = subsec->isec->hdr.type;

    assert(!subsec->added_to_osec);
    subsec->added_to_osec = true;
  }

  std::vector<Subsection<E> *> members;
  std::vector<std::unique_ptr<RangeExtensionThunk<E>>> thunks;
};

template <typename E>
class RebaseSection : public Chunk<E> {
public:
  RebaseSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__LINKEDIT", "__rebase") {
    this->is_hidden = true;
  }

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;

  std::vector<u8> contents;
};

template <typename E>
class BindSection : public Chunk<E> {
public:
  BindSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__LINKEDIT", "__binding") {
    this->is_hidden = true;
  }

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;

  std::vector<u8> contents;
};

template <typename E>
class LazyBindSection : public Chunk<E> {
public:
  LazyBindSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__LINKEDIT", "__lazy_binding") {
    this->is_hidden = true;
    this->hdr.p2align = 3;
  }

  void add(Context<E> &ctx, Symbol<E> &sym, i64 idx);

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;

  std::vector<u32> bind_offsets;
  std::vector<u8> contents;
};

class ExportEncoder {
public:
  i64 finish();

  struct Entry {
    std::string_view name;
    u32 flags;
    u64 addr;
  };

  struct TrieNode {
    std::string_view prefix;
    std::vector<std::unique_ptr<TrieNode>> children;
    u64 addr = 0;
    u32 flags = 0;
    u32 offset = -1;
    bool is_leaf = false;
  };

  void construct_trie(TrieNode &node, std::span<Entry> entries, i64 len,
                      tbb::task_group *tg, i64 grain_size, bool divide);

  static i64 set_offset(TrieNode &node, i64 offset);
  void write_trie(u8 *buf, TrieNode &node);

  TrieNode root;
  std::vector<Entry> entries;
};

template <typename E>
class ExportSection : public Chunk<E> {
public:
  ExportSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__LINKEDIT", "__export") {
    this->is_hidden = true;
  }

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;

private:
  ExportEncoder enc;
};

template <typename E>
class FunctionStartsSection : public Chunk<E> {
public:
  FunctionStartsSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__LINKEDIT", "__func_starts") {
    this->is_hidden = true;
  }

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;

  std::vector<u8> contents;
};

template <typename E>
class SymtabSection : public Chunk<E> {
public:
  SymtabSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__LINKEDIT", "__symbol_table") {
    this->is_hidden = true;
    this->hdr.p2align = 3;
  }

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;

  i64 globals_offset = 0;
  i64 undefs_offset = 0;

  static constexpr std::string_view strtab_init_image = " \0-\0"sv;
};

template <typename E>
class StrtabSection : public Chunk<E> {
public:
  StrtabSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__LINKEDIT", "__string_table") {
    this->is_hidden = true;
    this->hdr.p2align = 3;
    this->hdr.size = 1;
  }
};

template <typename E>
class IndirectSymtabSection : public Chunk<E> {
public:
  IndirectSymtabSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__LINKEDIT", "__ind_sym_tab") {
    this->is_hidden = true;
  }

  static constexpr i64 ENTRY_SIZE = 4;

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;
};

template <typename E>
class ObjcImageInfoSection : public Chunk<E> {
public:
  static std::unique_ptr<ObjcImageInfoSection> create(Context<E> &ctx);

  ObjcImageInfoSection(Context<E> &ctx, ObjcImageInfo contents)
    : Chunk<E>(ctx, "__DATA", "__objc_imageinfo"),
      contents(contents) {
    this->hdr.p2align = 2;
    this->hdr.size = sizeof(contents);
  }

  void copy_buf(Context<E> &ctx) override;

private:
  ObjcImageInfo contents;
};

template <typename E>
class ObjcStubsSection : public Chunk<E> {
public:
  ObjcStubsSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__TEXT", "__objc_stubs") {
    this->hdr.p2align = 5;
    this->hdr.attr = S_ATTR_SOME_INSTRUCTIONS | S_ATTR_PURE_INSTRUCTIONS;
  }

  void copy_buf(Context<E> &ctx) override;

  std::vector<Subsection<E> *> methnames;
  std::vector<Subsection<E> *> selrefs;

  static constexpr i64 ENTRY_SIZE = is_arm<E> ? 32 : 16;
};

template <typename E>
class InitOffsetsSection : public Chunk<E> {
public:
  InitOffsetsSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__TEXT", "__init_offsets") {
    this->hdr.p2align = 2;
    this->hdr.type = S_INIT_FUNC_OFFSETS;
  }

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;
};

template <typename E>
class CodeSignatureSection : public Chunk<E> {
public:
  CodeSignatureSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__LINKEDIT", "__code_signature") {
    this->is_hidden = true;
    this->hdr.p2align = 4;
  }

  void compute_size(Context<E> &ctx) override;
  void write_signature(Context<E> &ctx);
};

template <typename E>
class DataInCodeSection : public Chunk<E> {
public:
  DataInCodeSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__LINKEDIT", "__data_in_code") {
    this->is_hidden = true;
    this->hdr.p2align = 3;
  }

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;

  std::vector<DataInCodeEntry> contents;
};

template <typename E>
struct Fixup {
  u64 addr;
  Symbol<E> *sym = nullptr;
  u64 addend = 0;
};

template <typename E>
struct SymbolAddend {
  bool operator==(const SymbolAddend &) const = default;

  Symbol<E> *sym = nullptr;
  u64 addend = 0;
};

template <typename E>
class ChainedFixupsSection : public Chunk<E> {
public:
  ChainedFixupsSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__LINKEDIT", "__chainfixups") {
    this->is_hidden = true;
    this->hdr.p2align = 3;
  }

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;
  void write_fixup_chains(Context<E> &ctx);

private:
  u8 *allocate(i64 size);
  template <typename T> void write_imports(Context<E> &ctx);

  std::vector<Fixup<E>> fixups;
  std::vector<SymbolAddend<E>> dynsyms;
  std::vector<u8> contents;
};

template <typename E>
class StubsSection : public Chunk<E> {
public:
  StubsSection(Context<E> &ctx) : Chunk<E>(ctx, "__TEXT", "__stubs") {
    this->hdr.p2align = 4;
    this->hdr.type = S_SYMBOL_STUBS;
    this->hdr.attr = S_ATTR_SOME_INSTRUCTIONS | S_ATTR_PURE_INSTRUCTIONS;
    this->hdr.reserved1 = 0;
    this->hdr.reserved2 = E::stub_size;
  }

  void add(Context<E> &ctx, Symbol<E> *sym);
  void copy_buf(Context<E> &ctx) override;

  std::vector<Symbol<E> *> syms;
};

template <typename E>
class StubHelperSection : public Chunk<E> {
public:
  StubHelperSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__TEXT", "__stub_helper") {
    this->hdr.p2align = 4;
    this->hdr.attr = S_ATTR_SOME_INSTRUCTIONS | S_ATTR_PURE_INSTRUCTIONS;
  }

  void copy_buf(Context<E> &ctx) override;
};

template <typename E>
class UnwindInfoSection : public Chunk<E> {
public:
  UnwindInfoSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__TEXT", "__unwind_info") {
    this->hdr.p2align = 2;
  }

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;

  i64 num_lsda = 0;
  std::vector<Symbol<E> *> personalities;
  std::vector<std::vector<UnwindRecord<E> *>> pages;
};

template <typename E>
class GotSection : public Chunk<E> {
public:
  GotSection(Context<E> &ctx) : Chunk<E>(ctx, "__DATA_CONST", "__got") {
    this->hdr.p2align = 3;
    this->hdr.type = S_NON_LAZY_SYMBOL_POINTERS;
  }

  void add(Context<E> &ctx, Symbol<E> *sym);
  void copy_buf(Context<E> &ctx) override;

  std::vector<Symbol<E> *> syms;
};

template <typename E>
class LazySymbolPtrSection : public Chunk<E> {
public:
  LazySymbolPtrSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__DATA", "__la_symbol_ptr") {
    this->hdr.p2align = 3;
    this->hdr.type = S_LAZY_SYMBOL_POINTERS;
  }

  void copy_buf(Context<E> &ctx) override;
};

template <typename E>
class ThreadPtrsSection : public Chunk<E> {
public:
  ThreadPtrsSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__DATA", "__thread_ptrs") {
    this->hdr.p2align = 3;
    this->hdr.type = S_THREAD_LOCAL_VARIABLE_POINTERS;
  }

  void add(Context<E> &ctx, Symbol<E> *sym);
  void copy_buf(Context<E> &ctx) override;

  std::vector<Symbol<E> *> syms;
};

template <typename E>
class EhFrameSection : public Chunk<E> {
public:
  EhFrameSection(Context<E> &ctx)
    : Chunk<E>(ctx, "__TEXT", "__eh_frame") {
    this->hdr.p2align = sizeof(Word<E>);
  }

  void compute_size(Context<E> &ctx) override;
  void copy_buf(Context<E> &ctx) override;
};

template <typename E>
class SectCreateSection : public Chunk<E> {
public:
  SectCreateSection(Context<E> &ctx, std::string_view seg, std::string_view sect,
                    std::string_view contents);

  void copy_buf(Context<E> &ctx) override;

  std::string_view contents;
};

//
// mapfile.cc
//

template <typename E>
void print_map(Context<E> &ctx);

//
// yaml.cc
//

struct YamlNode {
  std::variant<std::string_view,
               std::vector<YamlNode>,
               std::map<std::string_view, YamlNode>> data;
};

struct YamlError {
  std::string msg;
  i64 pos;
};

std::variant<std::vector<YamlNode>, YamlError>
parse_yaml(std::string_view str);

//
// tapi.cc
//

struct TextDylib {
  std::string_view install_name;
  std::vector<std::string_view> reexported_libs;
  std::set<std::string_view> exports;
  std::set<std::string_view> weak_exports;
};

template <typename E>
TextDylib parse_tbd(Context<E> &ctx, MappedFile<Context<E>> *mf);

//
// cmdline.cc
//

struct VersionTriple {
  VersionTriple &operator=(const VersionTriple &) = default;
  auto operator<=>(const VersionTriple &) const = default;

  i64 encode() const { return (major << 16) | (minor << 8) | patch; }

  i64 major = 0;
  i64 minor = 0;
  i64 patch = 0;
};

template <typename E>
VersionTriple parse_version(Context<E> &ctx, std::string_view arg);

template <typename E>
std::vector<std::string> parse_nonpositional_args(Context<E> &ctx);

//
// dead-strip.cc
//

template <typename E>
void dead_strip(Context<E> &ctx);

//
// lto.cc
//

template <typename E>
void load_lto_plugin(Context<E> &ctx);

template <typename E>
void do_lto(Context<E> &ctx);

//
// arch-arm64.cc
//

template <typename E> requires is_arm<E>
void create_range_extension_thunks(Context<E> &ctx, OutputSection<E> &osec);

//
// main.cc
//

enum UuidKind { UUID_NONE, UUID_HASH, UUID_RANDOM };

struct AddEmptySectionOption {
  std::string_view segname;
  std::string_view sectname;
};

struct SectAlignOption {
  std::string_view segname;
  std::string_view sectname;
  u8 p2align;
};

struct SectCreateOption {
  std::string_view segname;
  std::string_view sectname;
  std::string_view filename;
};

struct ReaderContext {
  bool all_load = false;
  bool needed = false;
  bool hidden = false;
  bool weak = false;
  bool reexport = false;
  bool implicit = false;
};

template <typename E>
struct Context {
  Context() {
    text_seg = OutputSegment<E>::get_instance(*this, "__TEXT");
    data_const_seg = OutputSegment<E>::get_instance(*this, "__DATA_CONST");
    data_seg = OutputSegment<E>::get_instance(*this, "__DATA");
    linkedit_seg = OutputSegment<E>::get_instance(*this, "__LINKEDIT");

    text = OutputSection<E>::get_instance(*this, "__TEXT", "__text");
    data = OutputSection<E>::get_instance(*this, "__DATA", "__data");
    bss = OutputSection<E>::get_instance(*this, "__DATA", "__bss");
    common = OutputSection<E>::get_instance(*this, "__DATA", "__common");

    bss->hdr.type = S_ZEROFILL;
    common->hdr.type = S_ZEROFILL;

    dyld_stub_binder = get_symbol(*this, "dyld_stub_binder");
    _objc_msgSend = get_symbol(*this, "_objc_msgSend");
    __mh_execute_header = get_symbol(*this, "__mh_execute_header");
    __dyld_private = get_symbol(*this, "__dyld_private");
    __mh_dylib_header = get_symbol(*this, "__mh_dylib_header");
    __mh_bundle_header = get_symbol(*this, "__mh_bundle_header");
    ___dso_handle = get_symbol(*this, "___dso_handle");
  }

  Context(const Context<E> &) = delete;

  void checkpoint() {
    if (has_error) {
      cleanup();
      _exit(1);
    }
  }

  // Command-line arguments
  struct {
    MultiGlob exported_symbols_list;
    MultiGlob unexported_symbols_list;
    Symbol<E> *entry = nullptr;
    UuidKind uuid = UUID_HASH;
    VersionTriple compatibility_version;
    VersionTriple current_version;
    VersionTriple platform_min_version;
    VersionTriple platform_sdk_version;
    bool ObjC = false;
    bool S = false;
    bool adhoc_codesign = is_arm<E>;
    bool application_extension = false;
    bool bind_at_load = false;
    bool color_diagnostics = false;
    bool data_in_code_info = true;
    bool dead_strip = false;
    bool dead_strip_dylibs = false;
    bool deduplicate = true;
    bool demangle = true;
    bool dynamic = true;
    bool export_dynamic = false;
    bool fatal_warnings = false;
    bool fixup_chains = false;
    bool flat_namespace = false;
    bool function_starts = true;
    bool implicit_dylibs = true;
    bool init_offsets = false;
    bool mark_dead_strippable_dylib = false;
    bool noinhibit_exec = false;
    bool perf = false;
    bool print_dependencies = false;
    bool quick_exit = true;
    bool search_paths_first = true;
    bool stats = false;
    bool suppress_warnings = false;
    bool trace = false;
    bool undefined_error = true;
    bool x = false;
    i64 arch = CPU_TYPE_ARM64;
    i64 filler = 0;
    i64 headerpad = 256;
    i64 pagezero_size = 0;
    i64 platform = PLATFORM_MACOS;
    i64 stack_size = 0;
    i64 thread_count = 0;
    std::string bundle_loader;
    std::string chroot;
    std::string dependency_info;
    std::string executable_path;
    std::string final_output;
    std::string install_name;
    std::string lto_library;
    std::string map;
    std::string object_path_lto;
    std::string oso_prefix;
    std::string output = "a.out";
    std::string plugin;
    std::string umbrella;
    std::vector<AddEmptySectionOption> add_empty_section;
    std::vector<SectAlignOption> sectalign;
    std::vector<SectCreateOption> sectcreate;
    std::vector<std::string> U;
    std::vector<std::string> add_ast_path;
    std::vector<std::string> framework_paths;
    std::vector<std::string> library_paths;
    std::vector<std::string> mllvm;
    std::vector<std::string> order_file;
    std::vector<std::string> rpaths;
    std::vector<std::string> syslibroot;
    std::vector<std::string> u;
  } arg;

  ReaderContext reader;
  std::vector<std::string_view> cmdline_args;
  tbb::task_group tg;
  u32 output_type = MH_EXECUTE;
  i64 file_priority = 10000;
  std::set<std::string> missing_files; // for -dependency_info

  u8 uuid[16] = {};
  bool has_error = false;
  u64 tls_begin = 0;
  std::string cwd = std::filesystem::current_path().string();

  LTOPlugin lto = {};
  std::once_flag lto_plugin_loaded;

  tbb::concurrent_hash_map<std::string_view, Symbol<E>, HashCmp> symbol_map;

  std::unique_ptr<OutputFile<Context<E>>> output_file;
  u8 *buf;
  bool overwrite_output_file = false;

  tbb::concurrent_vector<std::unique_ptr<ObjectFile<E>>> obj_pool;
  tbb::concurrent_vector<std::unique_ptr<DylibFile<E>>> dylib_pool;
  tbb::concurrent_vector<std::unique_ptr<u8[]>> string_pool;
  tbb::concurrent_vector<std::unique_ptr<MappedFile<Context<E>>>> mf_pool;
  std::vector<std::unique_ptr<Chunk<E>>> chunk_pool;

  tbb::concurrent_vector<std::unique_ptr<TimerRecord>> timer_records;

  std::vector<ObjectFile<E> *> objs;
  std::vector<DylibFile<E> *> dylibs;
  ObjectFile<E> *internal_obj = nullptr;

  OutputSegment<E> *text_seg = nullptr;
  OutputSegment<E> *data_const_seg = nullptr;
  OutputSegment<E> *data_seg = nullptr;
  OutputSegment<E> *linkedit_seg = nullptr;

  std::vector<std::unique_ptr<OutputSegment<E>>> segments;
  std::vector<Chunk<E> *> chunks;

  OutputMachHeader<E> mach_hdr{*this};
  StubsSection<E> stubs{*this};
  UnwindInfoSection<E> unwind_info{*this};
  EhFrameSection<E> eh_frame{*this};
  GotSection<E> got{*this};
  ThreadPtrsSection<E> thread_ptrs{*this};
  ExportSection<E> export_{*this};
  SymtabSection<E> symtab{*this};
  StrtabSection<E> strtab{*this};
  IndirectSymtabSection<E> indir_symtab{*this};

  std::unique_ptr<StubHelperSection<E>> stub_helper;
  std::unique_ptr<LazySymbolPtrSection<E>> lazy_symbol_ptr;
  std::unique_ptr<LazyBindSection<E>> lazy_bind;
  std::unique_ptr<RebaseSection<E>> rebase;
  std::unique_ptr<BindSection<E>> bind;

  std::unique_ptr<ChainedFixupsSection<E>> chained_fixups;
  std::unique_ptr<FunctionStartsSection<E>> function_starts;
  std::unique_ptr<ObjcImageInfoSection<E>> image_info;
  std::unique_ptr<CodeSignatureSection<E>> code_sig;
  std::unique_ptr<ObjcStubsSection<E>> objc_stubs;
  std::unique_ptr<DataInCodeSection<E>> data_in_code;
  std::unique_ptr<InitOffsetsSection<E>> init_offsets;

  OutputSection<E> *text = nullptr;
  OutputSection<E> *data = nullptr;
  OutputSection<E> *bss = nullptr;
  OutputSection<E> *common = nullptr;

  Symbol<E> *dyld_stub_binder = nullptr;
  Symbol<E> *_objc_msgSend = nullptr;
  Symbol<E> *__mh_execute_header = nullptr;
  Symbol<E> *__dyld_private = nullptr;
  Symbol<E> *__mh_dylib_header = nullptr;
  Symbol<E> *__mh_bundle_header = nullptr;
  Symbol<E> *___dso_handle = nullptr;
};

template <typename E>
int macho_main(int argc, char **argv);

int main(int argc, char **argv);

//
// Inline functions
//

template <typename E>
std::ostream &operator<<(std::ostream &out, const InputSection<E> &sec) {
  out << sec.file << "(" << sec.hdr.get_segname() << ","
      << sec.hdr.get_sectname() << ")";
  return out;
}

template <typename E>
inline std::ostream &operator<<(std::ostream &out, const Symbol<E> &sym) {
  if (opt_demangle && sym.name.starts_with("__Z"))
    out << demangle(sym.name.substr(1));
  else
    out << sym.name;
  return out;
}

template <typename E>
inline Symbol<E> *get_symbol(Context<E> &ctx, std::string_view name) {
  typename decltype(ctx.symbol_map)::const_accessor acc;
  ctx.symbol_map.insert(acc, {name, Symbol<E>(name)});
  return (Symbol<E> *)(&acc->second);
}

} // namespace mold::macho



// 文件 macho.h
#pragma once

#include "../common/integers.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace mold::macho {

struct ARM64;
struct ARM64_32;
struct X86_64;

template <typename E> static constexpr bool is_arm64 = std::is_same_v<E, ARM64>;
template <typename E> static constexpr bool is_arm64_32 = std::is_same_v<E, ARM64_32>;
template <typename E> static constexpr bool is_arm = is_arm64<E> || is_arm64_32<E>;
template <typename E> static constexpr bool is_x86 = std::is_same_v<E, X86_64>;

template <typename E> using Word = std::conditional_t<is_arm64_32<E>, ul32, ul64>;
template <typename E> using IWord = std::conditional_t<is_arm64_32<E>, il32, il64>;

template <typename E>
std::string rel_to_string(u8 r_type);

enum : u32 {
  FAT_MAGIC = 0xcafebabe,
};

enum : u32 {
  MH_OBJECT = 0x1,
  MH_EXECUTE = 0x2,
  MH_FVMLIB = 0x3,
  MH_CORE = 0x4,
  MH_PRELOAD = 0x5,
  MH_DYLIB = 0x6,
  MH_DYLINKER = 0x7,
  MH_BUNDLE = 0x8,
  MH_DYLIB_STUB = 0x9,
  MH_DSYM = 0xa,
  MH_KEXT_BUNDLE = 0xb,
};

enum : u32 {
  MH_NOUNDEFS = 0x1,
  MH_INCRLINK = 0x2,
  MH_DYLDLINK = 0x4,
  MH_BINDATLOAD = 0x8,
  MH_PREBOUND = 0x10,
  MH_SPLIT_SEGS = 0x20,
  MH_LAZY_INIT = 0x40,
  MH_TWOLEVEL = 0x80,
  MH_FORCE_FLAT = 0x100,
  MH_NOMULTIDEFS = 0x200,
  MH_NOFIXPREBINDING = 0x400,
  MH_PREBINDABLE = 0x800,
  MH_ALLMODSBOUND = 0x1000,
  MH_SUBSECTIONS_VIA_SYMBOLS = 0x2000,
  MH_CANONICAL = 0x4000,
  MH_WEAK_DEFINES = 0x8000,
  MH_BINDS_TO_WEAK = 0x10000,
  MH_ALLOW_STACK_EXECUTION = 0x20000,
  MH_ROOT_SAFE = 0x40000,
  MH_SETUID_SAFE = 0x80000,
  MH_NO_REEXPORTED_DYLIBS = 0x100000,
  MH_PIE = 0x200000,
  MH_DEAD_STRIPPABLE_DYLIB = 0x400000,
  MH_HAS_TLV_DESCRIPTORS = 0x800000,
  MH_NO_HEAP_EXECUTION = 0x1000000,
  MH_APP_EXTENSION_SAFE = 0x02000000,
  MH_NLIST_OUTOFSYNC_WITH_DYLDINFO = 0x04000000,
  MH_SIM_SUPPORT = 0x08000000,
};

enum : u32 {
  VM_PROT_READ = 0x1,
  VM_PROT_WRITE = 0x2,
  VM_PROT_EXECUTE = 0x4,
  VM_PROT_NO_CHANGE = 0x8,
  VM_PROT_COPY = 0x10,
  VM_PROT_WANTS_COPY = 0x10,
};

enum : u32 {
  LC_REQ_DYLD = 0x80000000,
};

enum : u32 {
  LC_SEGMENT = 0x1,
  LC_SYMTAB = 0x2,
  LC_SYMSEG = 0x3,
  LC_THREAD = 0x4,
  LC_UNIXTHREAD = 0x5,
  LC_LOADFVMLIB = 0x6,
  LC_IDFVMLIB = 0x7,
  LC_IDENT = 0x8,
  LC_FVMFILE = 0x9,
  LC_PREPAGE = 0xa,
  LC_DYSYMTAB = 0xb,
  LC_LOAD_DYLIB = 0xc,
  LC_ID_DYLIB = 0xd,
  LC_LOAD_DYLINKER = 0xe,
  LC_ID_DYLINKER = 0xf,
  LC_PREBOUND_DYLIB = 0x10,
  LC_ROUTINES = 0x11,
  LC_SUB_FRAMEWORK = 0x12,
  LC_SUB_UMBRELLA = 0x13,
  LC_SUB_CLIENT = 0x14,
  LC_SUB_LIBRARY = 0x15,
  LC_TWOLEVEL_HINTS = 0x16,
  LC_PREBIND_CKSUM = 0x17,
  LC_LOAD_WEAK_DYLIB = (0x18 | LC_REQ_DYLD),
  LC_SEGMENT_64 = 0x19,
  LC_ROUTINES_64 = 0x1a,
  LC_UUID = 0x1b,
  LC_RPATH = (0x1c | LC_REQ_DYLD),
  LC_CODE_SIGNATURE = 0x1d,
  LC_SEGMENT_SPLIT_INFO = 0x1e,
  LC_REEXPORT_DYLIB = (0x1f | LC_REQ_DYLD),
  LC_LAZY_LOAD_DYLIB = 0x20,
  LC_ENCRYPTION_INFO = 0x21,
  LC_DYLD_INFO = 0x22,
  LC_DYLD_INFO_ONLY = (0x22 | LC_REQ_DYLD),
  LC_LOAD_UPWARD_DYLIB = (0x23 | LC_REQ_DYLD),
  LC_VERSION_MIN_MACOSX = 0x24,
  LC_VERSION_MIN_IPHONEOS = 0x25,
  LC_FUNCTION_STARTS = 0x26,
  LC_DYLD_ENVIRONMENT = 0x27,
  LC_MAIN = (0x28 | LC_REQ_DYLD),
  LC_DATA_IN_CODE = 0x29,
  LC_SOURCE_VERSION = 0x2A,
  LC_DYLIB_CODE_SIGN_DRS = 0x2B,
  LC_ENCRYPTION_INFO_64 = 0x2C,
  LC_LINKER_OPTION = 0x2D,
  LC_LINKER_OPTIMIZATION_HINT = 0x2E,
  LC_VERSION_MIN_TVOS = 0x2F,
  LC_VERSION_MIN_WATCHOS = 0x30,
  LC_NOTE = 0x31,
  LC_BUILD_VERSION = 0x32,
  LC_DYLD_EXPORTS_TRIE = (0x33 | LC_REQ_DYLD),
  LC_DYLD_CHAINED_FIXUPS = (0x34 | LC_REQ_DYLD),
};

enum : u32 {
  SG_HIGHVM = 0x1,
  SG_FVMLIB = 0x2,
  SG_NORELOC = 0x4,
  SG_PROTECTED_VERSION_1 = 0x8,
  SG_READ_ONLY = 0x10,
};

enum : u32 {
  S_REGULAR = 0x0,
  S_ZEROFILL = 0x1,
  S_CSTRING_LITERALS = 0x2,
  S_4BYTE_LITERALS = 0x3,
  S_8BYTE_LITERALS = 0x4,
  S_LITERAL_POINTERS = 0x5,
  S_NON_LAZY_SYMBOL_POINTERS = 0x6,
  S_LAZY_SYMBOL_POINTERS = 0x7,
  S_SYMBOL_STUBS = 0x8,
  S_MOD_INIT_FUNC_POINTERS = 0x9,
  S_MOD_TERM_FUNC_POINTERS = 0xa,
  S_COALESCED = 0xb,
  S_GB_ZEROFILL = 0xc,
  S_INTERPOSING = 0xd,
  S_16BYTE_LITERALS = 0xe,
  S_DTRACE_DOF = 0xf,
  S_LAZY_DYLIB_SYMBOL_POINTERS = 0x10,
  S_THREAD_LOCAL_REGULAR = 0x11,
  S_THREAD_LOCAL_ZEROFILL = 0x12,
  S_THREAD_LOCAL_VARIABLES = 0x13,
  S_THREAD_LOCAL_VARIABLE_POINTERS = 0x14,
  S_THREAD_LOCAL_INIT_FUNCTION_POINTERS = 0x15,
  S_INIT_FUNC_OFFSETS = 0x16,
};

enum : u32 {
  S_ATTR_LOC_RELOC = 0x1,
  S_ATTR_EXT_RELOC = 0x2,
  S_ATTR_SOME_INSTRUCTIONS = 0x4,
  S_ATTR_DEBUG = 0x20000,
  S_ATTR_SELF_MODIFYING_CODE = 0x40000,
  S_ATTR_LIVE_SUPPORT = 0x80000,
  S_ATTR_NO_DEAD_STRIP = 0x100000,
  S_ATTR_STRIP_STATIC_SYMS = 0x200000,
  S_ATTR_NO_TOC = 0x400000,
  S_ATTR_PURE_INSTRUCTIONS = 0x800000,
};

enum : u32 {
  CPU_TYPE_X86_64 = 0x1000007,
  CPU_TYPE_ARM64 = 0x100000c,
  CPU_TYPE_ARM64_32 = 0x200000c,
};

enum : u32 {
  CPU_SUBTYPE_X86_64_ALL = 3,
  CPU_SUBTYPE_ARM64_ALL = 0,
};

enum : u32 {
  INDIRECT_SYMBOL_LOCAL = 0x80000000,
  INDIRECT_SYMBOL_ABS = 0x40000000,
};

enum : u32 {
  REBASE_TYPE_POINTER = 1,
  REBASE_TYPE_TEXT_ABSOLUTE32 = 2,
  REBASE_TYPE_TEXT_PCREL32 = 3,
  REBASE_OPCODE_MASK = 0xf0,
  REBASE_IMMEDIATE_MASK = 0x0f,
  REBASE_OPCODE_DONE = 0x00,
  REBASE_OPCODE_SET_TYPE_IMM = 0x10,
  REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB = 0x20,
  REBASE_OPCODE_ADD_ADDR_ULEB = 0x30,
  REBASE_OPCODE_ADD_ADDR_IMM_SCALED = 0x40,
  REBASE_OPCODE_DO_REBASE_IMM_TIMES = 0x50,
  REBASE_OPCODE_DO_REBASE_ULEB_TIMES = 0x60,
  REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB = 0x70,
  REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB = 0x80,
};

enum : u32 {
  BIND_SPECIAL_DYLIB_SELF = 0,
  BIND_TYPE_POINTER = 1,
  BIND_TYPE_TEXT_ABSOLUTE32 = 2,
  BIND_TYPE_TEXT_PCREL32 = 3,
  BIND_SPECIAL_DYLIB_WEAK_LOOKUP = (u32)-3,
  BIND_SPECIAL_DYLIB_FLAT_LOOKUP = (u32)-2,
  BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE = (u32)-1,
  BIND_SYMBOL_FLAGS_WEAK_IMPORT = 1,
  BIND_SYMBOL_FLAGS_NON_WEAK_DEFINITION = 8,
  BIND_OPCODE_MASK = 0xF0,
  BIND_IMMEDIATE_MASK = 0x0F,
  BIND_OPCODE_DONE = 0x00,
  BIND_OPCODE_SET_DYLIB_ORDINAL_IMM = 0x10,
  BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB = 0x20,
  BIND_OPCODE_SET_DYLIB_SPECIAL_IMM = 0x30,
  BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM = 0x40,
  BIND_OPCODE_SET_TYPE_IMM = 0x50,
  BIND_OPCODE_SET_ADDEND_SLEB = 0x60,
  BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB = 0x70,
  BIND_OPCODE_ADD_ADDR_ULEB = 0x80,
  BIND_OPCODE_DO_BIND = 0x90,
  BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB = 0xA0,
  BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED = 0xB0,
  BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB = 0xC0,
  BIND_OPCODE_THREADED = 0xD0,
  BIND_SUBOPCODE_THREADED_SET_BIND_ORDINAL_TABLE_SIZE_ULEB = 0x00,
  BIND_SUBOPCODE_THREADED_APPLY = 0x01,
};

enum : u32 {
  EXPORT_SYMBOL_FLAGS_KIND_MASK = 0x03,
  EXPORT_SYMBOL_FLAGS_KIND_REGULAR = 0x00,
  EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL = 0x01,
  EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE = 0x02,
  EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION = 0x04,
  EXPORT_SYMBOL_FLAGS_REEXPORT = 0x08,
  EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER = 0x10,
};

enum : u32 {
  DICE_KIND_DATA = 1,
  DICE_KIND_JUMP_TABLE8 = 2,
  DICE_KIND_JUMP_TABLE16 = 3,
  DICE_KIND_JUMP_TABLE32 = 4,
  DICE_KIND_ABS_JUMP_TABLE32 = 5,
};

enum : u32 {
  N_UNDF = 0,
  N_ABS = 1,
  N_INDR = 5,
  N_PBUD = 6,
  N_SECT = 7,
};

enum : u32 {
  N_GSYM = 0x20,
  N_FNAME = 0x22,
  N_FUN = 0x24,
  N_STSYM = 0x26,
  N_LCSYM = 0x28,
  N_BNSYM = 0x2e,
  N_AST = 0x32,
  N_OPT = 0x3c,
  N_RSYM = 0x40,
  N_SLINE = 0x44,
  N_ENSYM = 0x4e,
  N_SSYM = 0x60,
  N_SO = 0x64,
  N_OSO = 0x66,
  N_LSYM = 0x80,
  N_BINCL = 0x82,
  N_SOL = 0x84,
  N_PARAMS = 0x86,
  N_VERSION = 0x88,
  N_OLEVEL = 0x8A,
  N_PSYM = 0xa0,
  N_EINCL = 0xa2,
  N_ENTRY = 0xa4,
  N_LBRAC = 0xc0,
  N_EXCL = 0xc2,
  N_RBRAC = 0xe0,
  N_BCOMM = 0xe2,
  N_ECOMM = 0xe4,
  N_ECOML = 0xe8,
  N_LENG = 0xfe,
  N_PC = 0x30,
};

enum : u32 {
  REFERENCE_TYPE = 0xf,
  REFERENCE_FLAG_UNDEFINED_NON_LAZY = 0,
  REFERENCE_FLAG_UNDEFINED_LAZY = 1,
  REFERENCE_FLAG_DEFINED = 2,
  REFERENCE_FLAG_PRIVATE_DEFINED = 3,
  REFERENCE_FLAG_PRIVATE_UNDEFINED_NON_LAZY = 4,
  REFERENCE_FLAG_PRIVATE_UNDEFINED_LAZY = 5,
};

enum : u32 {
  REFERENCED_DYNAMICALLY = 0x0010,
};

enum : u32 {
  SELF_LIBRARY_ORDINAL = 0x0,
  MAX_LIBRARY_ORDINAL = 0xfd,
  DYNAMIC_LOOKUP_ORDINAL = 0xfe,
  EXECUTABLE_ORDINAL = 0xff,
};

enum : u32 {
  N_NO_DEAD_STRIP = 0x0020,
  N_DESC_DISCARDED = 0x0020,
  N_WEAK_REF = 0x0040,
  N_WEAK_DEF = 0x0080,
  N_REF_TO_WEAK = 0x0080,
  N_ARM_THUMB_DEF = 0x0008,
  N_SYMBOL_RESOLVER = 0x0100,
  N_ALT_ENTRY = 0x0200,
};

enum : u32 {
  PLATFORM_MACOS = 1,
  PLATFORM_IOS = 2,
  PLATFORM_TVOS = 3,
  PLATFORM_WATCHOS = 4,
  PLATFORM_BRIDGEOS = 5,
  PLATFORM_MACCATALYST = 6,
  PLATFORM_IOSSIMULATOR = 7,
  PLATFORM_TVOSSIMULATOR = 8,
  PLATFORM_WATCHOSSIMULATOR = 9,
  PLATFORM_DRIVERKIT = 10,
};

enum : u32 {
  TOOL_CLANG = 1,
  TOOL_SWIFT = 2,
  TOOL_LD = 3,
  TOOL_MOLD = 54321, // Randomly chosen
};

enum : u32 {
  OBJC_IMAGE_SUPPORTS_GC = 1 << 1,
  OBJC_IMAGE_REQUIRES_GC = 1 << 2,
  OBJC_IMAGE_OPTIMIZED_BY_DYLD = 1 << 3,
  OBJC_IMAGE_SUPPORTS_COMPACTION = 1 << 4,
  OBJC_IMAGE_IS_SIMULATED = 1 << 5,
  OBJC_IMAGE_HAS_CATEGORY_CLASS_PROPERTIES = 1 << 6,
};

enum : u32 {
  LOH_ARM64_ADRP_ADRP = 1,
  LOH_ARM64_ADRP_LDR = 2,
  LOH_ARM64_ADRP_ADD_LDR = 3,
  LOH_ARM64_ADRP_LDR_GOT_LDR = 4,
  LOH_ARM64_ADRP_ADD_STR = 5,
  LOH_ARM64_ADRP_LDR_GOT_STR = 6,
  LOH_ARM64_ADRP_ADD = 7,
  LOH_ARM64_ADRP_LDR_GOT = 8,
};

enum : u32 {
  ARM64_RELOC_UNSIGNED = 0,
  ARM64_RELOC_SUBTRACTOR = 1,
  ARM64_RELOC_BRANCH26 = 2,
  ARM64_RELOC_PAGE21 = 3,
  ARM64_RELOC_PAGEOFF12 = 4,
  ARM64_RELOC_GOT_LOAD_PAGE21 = 5,
  ARM64_RELOC_GOT_LOAD_PAGEOFF12 = 6,
  ARM64_RELOC_POINTER_TO_GOT = 7,
  ARM64_RELOC_TLVP_LOAD_PAGE21 = 8,
  ARM64_RELOC_TLVP_LOAD_PAGEOFF12 = 9,
  ARM64_RELOC_ADDEND = 10,
};

enum : u32 {
  X86_64_RELOC_UNSIGNED = 0,
  X86_64_RELOC_SIGNED = 1,
  X86_64_RELOC_BRANCH = 2,
  X86_64_RELOC_GOT_LOAD = 3,
  X86_64_RELOC_GOT = 4,
  X86_64_RELOC_SUBTRACTOR = 5,
  X86_64_RELOC_SIGNED_1 = 6,
  X86_64_RELOC_SIGNED_2 = 7,
  X86_64_RELOC_SIGNED_4 = 8,
  X86_64_RELOC_TLV = 9,
};

//
// DWARF data types
//

enum : u32 {
  DW_EH_PE_absptr = 0,
  DW_EH_PE_omit = 0xff,
  DW_EH_PE_uleb128 = 0x01,
  DW_EH_PE_udata2 = 0x02,
  DW_EH_PE_udata4 = 0x03,
  DW_EH_PE_udata8 = 0x04,
  DW_EH_PE_signed = 0x08,
  DW_EH_PE_sleb128 = 0x09,
  DW_EH_PE_sdata2 = 0x0a,
  DW_EH_PE_sdata4 = 0x0b,
  DW_EH_PE_sdata8 = 0x0c,
  DW_EH_PE_pcrel = 0x10,
  DW_EH_PE_textrel = 0x20,
  DW_EH_PE_datarel = 0x30,
  DW_EH_PE_funcrel = 0x40,
  DW_EH_PE_aligned = 0x50,
  DW_EH_PE_indirect = 0x80,
};

enum : u32 {
  DW_AT_name = 0x3,
  DW_AT_low_pc = 0x11,
  DW_AT_high_pc = 0x12,
  DW_AT_producer = 0x25,
  DW_AT_ranges = 0x55,
  DW_AT_addr_base = 0x73,
  DW_AT_rnglists_base = 0x74,
};

enum : u32 {
  DW_TAG_compile_unit = 0x11,
  DW_TAG_skeleton_unit = 0x4a,
};

enum : u32 {
  DW_UT_compile = 0x01,
  DW_UT_partial = 0x03,
  DW_UT_skeleton = 0x04,
  DW_UT_split_compile = 0x05,
};

enum : u32 {
  DW_FORM_addr = 0x01,
  DW_FORM_block2 = 0x03,
  DW_FORM_block4 = 0x04,
  DW_FORM_data2 = 0x05,
  DW_FORM_data4 = 0x06,
  DW_FORM_data8 = 0x07,
  DW_FORM_string = 0x08,
  DW_FORM_block = 0x09,
  DW_FORM_block1 = 0x0a,
  DW_FORM_data1 = 0x0b,
  DW_FORM_flag = 0x0c,
  DW_FORM_sdata = 0x0d,
  DW_FORM_strp = 0x0e,
  DW_FORM_udata = 0x0f,
  DW_FORM_ref_addr = 0x10,
  DW_FORM_ref1 = 0x11,
  DW_FORM_ref2 = 0x12,
  DW_FORM_ref4 = 0x13,
  DW_FORM_ref8 = 0x14,
  DW_FORM_ref_udata = 0x15,
  DW_FORM_indirect = 0x16,
  DW_FORM_sec_offset = 0x17,
  DW_FORM_exprloc = 0x18,
  DW_FORM_flag_present = 0x19,
  DW_FORM_strx = 0x1a,
  DW_FORM_addrx = 0x1b,
  DW_FORM_ref_sup4 = 0x1c,
  DW_FORM_strp_sup = 0x1d,
  DW_FORM_data16 = 0x1e,
  DW_FORM_line_strp = 0x1f,
  DW_FORM_ref_sig8 = 0x20,
  DW_FORM_implicit_const = 0x21,
  DW_FORM_loclistx = 0x22,
  DW_FORM_rnglistx = 0x23,
  DW_FORM_ref_sup8 = 0x24,
  DW_FORM_strx1 = 0x25,
  DW_FORM_strx2 = 0x26,
  DW_FORM_strx3 = 0x27,
  DW_FORM_strx4 = 0x28,
  DW_FORM_addrx1 = 0x29,
  DW_FORM_addrx2 = 0x2a,
  DW_FORM_addrx3 = 0x2b,
  DW_FORM_addrx4 = 0x2c,
};

enum : u32 {
  DW_RLE_end_of_list = 0x00,
  DW_RLE_base_addressx = 0x01,
  DW_RLE_startx_endx = 0x02,
  DW_RLE_startx_length = 0x03,
  DW_RLE_offset_pair = 0x04,
  DW_RLE_base_address = 0x05,
  DW_RLE_start_end = 0x06,
  DW_RLE_start_length = 0x07,
};

//
// Mach-O types
//

template <>
inline std::string rel_to_string<ARM64>(u8 type) {
  switch (type) {
  case ARM64_RELOC_UNSIGNED: return "ARM64_RELOC_UNSIGNED";
  case ARM64_RELOC_SUBTRACTOR: return "ARM64_RELOC_SUBTRACTOR";
  case ARM64_RELOC_BRANCH26: return "ARM64_RELOC_BRANCH26";
  case ARM64_RELOC_PAGE21: return "ARM64_RELOC_PAGE21";
  case ARM64_RELOC_PAGEOFF12: return "ARM64_RELOC_PAGEOFF12";
  case ARM64_RELOC_GOT_LOAD_PAGE21: return "ARM64_RELOC_GOT_LOAD_PAGE21";
  case ARM64_RELOC_GOT_LOAD_PAGEOFF12: return "ARM64_RELOC_GOT_LOAD_PAGEOFF12";
  case ARM64_RELOC_POINTER_TO_GOT: return "ARM64_RELOC_POINTER_TO_GOT";
  case ARM64_RELOC_TLVP_LOAD_PAGE21: return "ARM64_RELOC_TLVP_LOAD_PAGE21";
  case ARM64_RELOC_TLVP_LOAD_PAGEOFF12: return "ARM64_RELOC_TLVP_LOAD_PAGEOFF12";
  case ARM64_RELOC_ADDEND: return "ARM64_RELOC_ADDEND";
  }
  return "unknown (" + std::to_string(type) + ")";
}

template <>
inline std::string rel_to_string<X86_64>(u8 type) {
  switch (type) {
  case X86_64_RELOC_UNSIGNED: return "X86_64_RELOC_UNSIGNED";
  case X86_64_RELOC_SIGNED: return "X86_64_RELOC_SIGNED";
  case X86_64_RELOC_BRANCH: return "X86_64_RELOC_BRANCH";
  case X86_64_RELOC_GOT_LOAD: return "X86_64_RELOC_GOT_LOAD";
  case X86_64_RELOC_GOT: return "X86_64_RELOC_GOT";
  case X86_64_RELOC_SUBTRACTOR: return "X86_64_RELOC_SUBTRACTOR";
  case X86_64_RELOC_SIGNED_1: return "X86_64_RELOC_SIGNED_1";
  case X86_64_RELOC_SIGNED_2: return "X86_64_RELOC_SIGNED_2";
  case X86_64_RELOC_SIGNED_4: return "X86_64_RELOC_SIGNED_4";
  case X86_64_RELOC_TLV: return "X86_64_RELOC_TLV";
  }
  return "unknown (" + std::to_string(type) + ")";
}

struct FatHeader {
  ub32 magic;
  ub32 nfat_arch;
};

struct FatArch {
  ub32 cputype;
  ub32 cpusubtype;
  ub32 offset;
  ub32 size;
  ub32 align;
};

struct MachHeader {
  ul32 magic;
  ul32 cputype;
  ul32 cpusubtype;
  ul32 filetype;
  ul32 ncmds;
  ul32 sizeofcmds;
  ul32 flags;
  ul32 reserved;
};

struct LoadCommand {
  ul32 cmd;
  ul32 cmdsize;
};

template <typename E>
struct SegmentCommand {
  std::string_view get_segname() const {
    return {segname, strnlen(segname, sizeof(segname))};
  }

  ul32 cmd;
  ul32 cmdsize;
  char segname[16];
  Word<E> vmaddr;
  Word<E> vmsize;
  Word<E> fileoff;
  Word<E> filesize;
  ul32 maxprot;
  ul32 initprot;
  ul32 nsects;
  ul32 flags;
};

template <typename E>
struct MachSection {
  void set_segname(std::string_view name) {
    assert(name.size() <= sizeof(segname));
    memcpy(segname, name.data(), name.size());
  }

  std::string_view get_segname() const {
    return {segname, strnlen(segname, sizeof(segname))};
  }

  void set_sectname(std::string_view name) {
    assert(name.size() <= sizeof(sectname));
    memcpy(sectname, name.data(), name.size());
  }

  std::string_view get_sectname() const {
    return {sectname, strnlen(sectname, sizeof(sectname))};
  }

  bool match(std::string_view segname, std::string_view sectname) const {
    return get_segname() == segname && get_sectname() == sectname;
  }

  bool is_text() const {
    return (attr & S_ATTR_SOME_INSTRUCTIONS) || (attr & S_ATTR_PURE_INSTRUCTIONS);
  }

  char sectname[16];
  char segname[16];
  Word<E> addr;
  Word<E> size;
  ul32 offset;
  ul32 p2align;
  ul32 reloff;
  ul32 nreloc;
  u8 type;
  ul24 attr;
  ul32 reserved1;
  ul32 reserved2;
  ul32 reserved3;
};

struct DylibCommand {
  ul32 cmd;
  ul32 cmdsize;
  ul32 nameoff;
  ul32 timestamp;
  ul32 current_version;
  ul32 compatibility_version;
};

struct DylinkerCommand {
  ul32 cmd;
  ul32 cmdsize;
  ul32 nameoff;
};

struct SymtabCommand {
  ul32 cmd;
  ul32 cmdsize;
  ul32 symoff;
  ul32 nsyms;
  ul32 stroff;
  ul32 strsize;
};

struct DysymtabCommand {
  ul32 cmd;
  ul32 cmdsize;
  ul32 ilocalsym;
  ul32 nlocalsym;
  ul32 iextdefsym;
  ul32 nextdefsym;
  ul32 iundefsym;
  ul32 nundefsym;
  ul32 tocoff;
  ul32 ntoc;
  ul32 modtaboff;
  ul32 nmodtab;
  ul32 extrefsymoff;
  ul32 nextrefsyms;
  ul32 indirectsymoff;
  ul32 nindirectsyms;
  ul32 extreloff;
  ul32 nextrel;
  ul32 locreloff;
  ul32 nlocrel;
};

struct VersionMinCommand {
  ul32 cmd;
  ul32 cmdsize;
  ul32 version;
  ul32 sdk;
};

struct DyldInfoCommand {
  ul32 cmd;
  ul32 cmdsize;
  ul32 rebase_off;
  ul32 rebase_size;
  ul32 bind_off;
  ul32 bind_size;
  ul32 weak_bind_off;
  ul32 weak_bind_size;
  ul32 lazy_bind_off;
  ul32 lazy_bind_size;
  ul32 export_off;
  ul32 export_size;
};

struct UUIDCommand {
  ul32 cmd;
  ul32 cmdsize;
  u8 uuid[16];
};

struct RpathCommand {
  ul32 cmd;
  ul32 cmdsize;
  ul32 path_off;
};

struct LinkEditDataCommand {
  ul32 cmd;
  ul32 cmdsize;
  ul32 dataoff;
  ul32 datasize;
};

struct BuildToolVersion {
  ul32 tool;
  ul32 version;
};

struct BuildVersionCommand {
  ul32 cmd;
  ul32 cmdsize;
  ul32 platform;
  ul32 minos;
  ul32 sdk;
  ul32 ntools;
};

struct EntryPointCommand {
  ul32 cmd;
  ul32 cmdsize;
  ul64 entryoff;
  ul64 stacksize;
};

struct SourceVersionCommand {
  ul32 cmd;
  ul32 cmdsize;
  ul64 version;
};

struct DataInCodeEntry {
  ul32 offset;
  ul16 length;
  ul16 kind;
};

struct UmbrellaCommand {
  ul32 cmd;
  ul32 cmdsize;
  ul32 umbrella_off;
};

struct LinkerOptionCommand {
  ul32 cmd;
  ul32 cmdsize;
  ul32 count;
};

// This struct is named `n_list` on BSD and macOS.
template <typename E>
struct MachSym {
  bool is_undef() const {
    return type == N_UNDF && !is_common();
  }

  bool is_common() const {
    return type == N_UNDF && is_extern && value;
  }

  ul32 stroff;

  union {
    u8 n_type;
    struct {
      u8 is_extern : 1;
      u8 type : 3;
      u8 is_private_extern : 1;
      u8 stab : 3;
    };
  };

  u8 sect;

  union {
    ul16 desc;
    struct {
      u8 padding;
      u8 common_p2align : 4;
    };
  };

  Word<E> value;
};

// This struct is named `relocation_info` on BSD and macOS.
struct MachRel {
  ul32 offset;
  ul24 idx;
  u8 is_pcrel : 1;
  u8 p2size : 2;
  u8 is_extern : 1;
  u8 type : 4;
};

// __TEXT,__unwind_info section contents

enum : u32 {
  UNWIND_SECTION_VERSION = 1,
  UNWIND_SECOND_LEVEL_REGULAR = 2,
  UNWIND_SECOND_LEVEL_COMPRESSED = 3,
  UNWIND_PERSONALITY_MASK = 0x30000000,
};

enum : u32 {
  UNWIND_MODE_MASK = 0x0f000000,
  UNWIND_ARM64_MODE_DWARF = 0x03000000,
  UNWIND_X86_64_MODE_STACK_IND = 0x03000000,
  UNWIND_X86_64_MODE_DWARF = 0x04000000,
};

struct UnwindSectionHeader {
  ul32 version;
  ul32 encoding_offset;
  ul32 encoding_count;
  ul32 personality_offset;
  ul32 personality_count;
  ul32 page_offset;
  ul32 page_count;
};

struct UnwindFirstLevelPage {
  ul32 func_addr;
  ul32 page_offset;
  ul32 lsda_offset;
};

struct UnwindSecondLevelPage {
  ul32 kind;
  ul16 page_offset;
  ul16 page_count;
  ul16 encoding_offset;
  ul16 encoding_count;
};

struct UnwindLsdaEntry {
  ul32 func_addr;
  ul32 lsda_addr;
};

struct UnwindPageEntry {
  ul24 func_addr;
  u8 encoding;
};

// __LD,__compact_unwind section contents
template <typename E>
struct CompactUnwindEntry {
  Word<E> code_start;
  ul32 code_len;
  ul32 encoding;
  Word<E> personality;
  Word<E> lsda;
};

// __LINKEDIT,__code_signature

enum : u32 {
  CSMAGIC_EMBEDDED_SIGNATURE = 0xfade0cc0,
  CS_SUPPORTSEXECSEG = 0x20400,
  CSMAGIC_CODEDIRECTORY = 0xfade0c02,
  CSSLOT_CODEDIRECTORY = 0,
  CS_ADHOC = 0x00000002,
  CS_LINKER_SIGNED = 0x00020000,
  CS_EXECSEG_MAIN_BINARY = 1,
  CS_HASHTYPE_SHA256 = 2,
};

struct CodeSignatureHeader {
  ub32 magic;
  ub32 length;
  ub32 count;
};

struct CodeSignatureBlobIndex {
  ub32 type;
  ub32 offset;
  ub32 padding;
};

struct CodeSignatureDirectory {
  ub32 magic;
  ub32 length;
  ub32 version;
  ub32 flags;
  ub32 hash_offset;
  ub32 ident_offset;
  ub32 n_special_slots;
  ub32 n_code_slots;
  ub32 code_limit;
  u8 hash_size;
  u8 hash_type;
  u8 platform;
  u8 page_size;
  ub32 spare2;
  ub32 scatter_offset;
  ub32 team_offset;
  ub32 spare3;
  ub64 code_limit64;
  ub64 exec_seg_base;
  ub64 exec_seg_limit;
  ub64 exec_seg_flags;
};

// __DATA,__objc_imageinfo
struct ObjcImageInfo {
  ul32 version = 0;
  u8 flags = 0;
  u8 swift_version = 0;
  ul16 swift_lang_version = 0;
};

// __LINKEDIT,__chainfixups
struct DyldChainedFixupsHeader {
  ul32 fixups_version;
  ul32 starts_offset;
  ul32 imports_offset;
  ul32 symbols_offset;
  ul32 imports_count;
  ul32 imports_format;
  ul32 symbols_format;
};

struct DyldChainedStartsInImage {
  ul32 seg_count;
  ul32 seg_info_offset[];
};

struct DyldChainedStartsInSegment {
  ul32 size;
  ul16 page_size;
  ul16 pointer_format;
  ul64 segment_offset;
  ul32 max_valid_pointer;
  ul16 page_count;
  ul16 page_start[];
};

struct DyldChainedPtr64Rebase {
  u64 target   : 36;
  u64 high8    :  8;
  u64 reserved :  7;
  u64 next     : 12;
  u64 bind     :  1;
};

struct DyldChainedPtr64Bind {
  u64 ordinal  : 24;
  u64 addend   :  8;
  u64 reserved : 19;
  u64 next     : 12;
  u64 bind     :  1;
};

struct DyldChainedImport {
  u32 lib_ordinal :  8;
  u32 weak_import :  1;
  u32 name_offset : 23;
};

struct DyldChainedImportAddend {
  u32 lib_ordinal :  8;
  u32 weak_import :  1;
  u32 name_offset : 23;
  u32 addend;
};

struct DyldChainedImportAddend64 {
  u64 lib_ordinal : 16;
  u64 weak_import :  1;
  u64 reserved    : 15;
  u64 name_offset : 32;
  u64 addend;
};

enum : u32 {
  DYLD_CHAINED_PTR_ARM64E = 1,
  DYLD_CHAINED_PTR_64 = 2,
  DYLD_CHAINED_PTR_32 = 3,
  DYLD_CHAINED_PTR_32_CACHE = 4,
  DYLD_CHAINED_PTR_32_FIRMWARE = 5,
  DYLD_CHAINED_PTR_START_NONE = 0xFFFF,
  DYLD_CHAINED_PTR_START_MULTI = 0x8000,
  DYLD_CHAINED_PTR_START_LAST = 0x8000,
};

enum : u32 {
  DYLD_CHAINED_IMPORT = 1,
  DYLD_CHAINED_IMPORT_ADDEND = 2,
  DYLD_CHAINED_IMPORT_ADDEND64 = 3,
};

enum : u32 {
  DYLD_CHAINED_PTR_64_OFFSET = 6,
  DYLD_CHAINED_PTR_ARM64E_OFFSET = 7,
};

struct ARM64 {
  static constexpr u32 cputype = CPU_TYPE_ARM64;
  static constexpr u32 cpusubtype = CPU_SUBTYPE_ARM64_ALL;
  static constexpr u32 page_size = 16384;
  static constexpr u32 abs_rel = ARM64_RELOC_UNSIGNED;
  static constexpr u32 subtractor_rel = ARM64_RELOC_SUBTRACTOR;
  static constexpr u32 gotpc_rel = ARM64_RELOC_POINTER_TO_GOT;
  static constexpr u32 stub_size = 12;
  static constexpr u32 stub_helper_hdr_size = 24;
  static constexpr u32 stub_helper_size = 12;
  static constexpr u32 unwind_mode_dwarf = UNWIND_ARM64_MODE_DWARF;
};

struct ARM64_32 {
  static constexpr u32 cputype = CPU_TYPE_ARM64_32;
  static constexpr u32 cpusubtype = CPU_SUBTYPE_ARM64_ALL;
  static constexpr u32 page_size = 16384;
  static constexpr u32 abs_rel = ARM64_RELOC_UNSIGNED;
  static constexpr u32 subtractor_rel = ARM64_RELOC_SUBTRACTOR;
  static constexpr u32 gotpc_rel = ARM64_RELOC_POINTER_TO_GOT;
  static constexpr u32 stub_size = 12;
  static constexpr u32 stub_helper_hdr_size = 24;
  static constexpr u32 stub_helper_size = 12;
  static constexpr u32 unwind_mode_dwarf = UNWIND_ARM64_MODE_DWARF;
};

struct X86_64 {
  static constexpr u32 cputype = CPU_TYPE_X86_64;
  static constexpr u32 cpusubtype = CPU_SUBTYPE_X86_64_ALL;
  static constexpr u32 page_size = 4096;
  static constexpr u32 abs_rel = X86_64_RELOC_UNSIGNED;
  static constexpr u32 subtractor_rel = X86_64_RELOC_SUBTRACTOR;
  static constexpr u32 gotpc_rel = X86_64_RELOC_GOT;
  static constexpr u32 stub_size = 6;
  static constexpr u32 stub_helper_hdr_size = 16;
  static constexpr u32 stub_helper_size = 10;
  static constexpr u32 unwind_mode_dwarf = UNWIND_X86_64_MODE_DWARF;
};

} // namespace mold::macho

// 文件 arch-x864-64.cc
#include "mold.h"

namespace mold::macho {

using E = X86_64;

template <>
void StubsSection<E>::copy_buf(Context<E> &ctx) {
  u8 *buf = ctx.buf + this->hdr.offset;

  static const u8 insn[] = {
    0xff, 0x25, 0, 0, 0, 0, // jmp *imm(%rip)
  };

  static_assert(E::stub_size == sizeof(insn));

  for (i64 i = 0; i < syms.size(); i++) {
    memcpy(buf, insn, sizeof(insn));

    u64 this_addr = this->hdr.addr + i * 6;

    u64 ptr_addr;
    if (ctx.lazy_symbol_ptr)
      ptr_addr = ctx.lazy_symbol_ptr->hdr.addr + sizeof(Word<E>) * i;
    else
      ptr_addr = syms[i]->get_got_addr(ctx);

    *(ul32 *)(buf + 2) = ptr_addr - this_addr - 6;
    buf += sizeof(insn);
  }
}

template <>
void StubHelperSection<E>::copy_buf(Context<E> &ctx) {
  u8 *start = ctx.buf + this->hdr.offset;
  u8 *buf = start;

  static const u8 insn0[] = {
    0x4c, 0x8d, 0x1d, 0, 0, 0, 0, // lea $__dyld_private(%rip), %r11
    0x41, 0x53,                   // push %r11
    0xff, 0x25, 0, 0, 0, 0,       // jmp *$dyld_stub_binder@GOT(%rip)
    0x90,                         // nop
  };

  static_assert(sizeof(insn0) == E::stub_helper_hdr_size);

  memcpy(buf, insn0, sizeof(insn0));
  *(ul32 *)(buf + 3) = ctx.__dyld_private->get_addr(ctx) - this->hdr.addr - 7;
  *(ul32 *)(buf + 11) =
    ctx.dyld_stub_binder->get_got_addr(ctx) - this->hdr.addr - 15;

  buf += sizeof(insn0);

  for (i64 i = 0; i < ctx.stubs.syms.size(); i++) {
    u8 insn[] = {
      0x68, 0, 0, 0, 0, // push $bind_offset
      0xe9, 0, 0, 0, 0, // jmp $__stub_helper
    };

    static_assert(sizeof(insn) == E::stub_helper_size);

    memcpy(buf, insn, sizeof(insn));
    *(ul32 *)(buf + 1) = ctx.lazy_bind->bind_offsets[i];
    *(ul32 *)(buf + 6) = start - buf - 10;
    buf += sizeof(insn);
  }
}

template <>
void ObjcStubsSection<E>::copy_buf(Context<E> &ctx) {
  if (this->hdr.size == 0)
    return;

  static const u8 insn[] = {
    0x48, 0x8b, 0x35, 0, 0, 0, 0, // mov @selector("foo")(%rip), %rsi
    0xff, 0x25, 0, 0, 0, 0,       // jmp *_objc_msgSend@GOT(%rip)
    0xcc, 0xcc, 0xcc,             // (padding)
  };
  static_assert(sizeof(insn) == ENTRY_SIZE);

  u64 msgsend_got_addr = ctx._objc_msgSend->get_got_addr(ctx);

  for (i64 i = 0; i < methnames.size(); i++) {
    ul32 *buf = (ul32 *)(ctx.buf + this->hdr.offset + ENTRY_SIZE * i);
    u64 sel_addr = selrefs[i]->get_addr(ctx);
    u64 ent_addr = this->hdr.addr + ENTRY_SIZE * i;

    memcpy(buf, insn, sizeof(insn));
    *(ul32 *)(buf + 3) = sel_addr - ent_addr - 7;
    *(ul32 *)(buf + 9) = msgsend_got_addr - ent_addr - 13;
  }
}

static i64 get_reloc_addend(u32 type) {
  switch (type) {
  case X86_64_RELOC_SIGNED_1:
    return 1;
  case X86_64_RELOC_SIGNED_2:
    return 2;
  case X86_64_RELOC_SIGNED_4:
    return 4;
  default:
    return 0;
  }
}

static i64 read_addend(u8 *buf, const MachRel &r) {
  if (r.p2size == 2)
    return *(il32 *)(buf + r.offset);
  assert(r.p2size == 3);
  return *(il64 *)(buf + r.offset);
}

template <>
std::vector<Relocation<E>>
read_relocations(Context<E> &ctx, ObjectFile<E> &file, const MachSection<E> &hdr) {
  std::vector<Relocation<E>> vec;
  vec.reserve(hdr.nreloc);

  MachRel *rels = (MachRel *)(file.mf->data + hdr.reloff);

  for (i64 i = 0; i < hdr.nreloc; i++) {
    MachRel &r = rels[i];
    i64 addend = read_addend(file.mf->data + hdr.offset, r) +
                 get_reloc_addend(r.type);

    vec.push_back(Relocation<E>{
      .offset = r.offset,
      .type = (u8)r.type,
      .size = (u8)(1 << r.p2size),
      .is_subtracted = (i > 0 && rels[i - 1].type == X86_64_RELOC_SUBTRACTOR),
    });

    Relocation<E> &rel = vec.back();

    if (r.is_extern) {
      rel.target = file.syms[r.idx];
      rel.is_sym = true;
      rel.addend = addend;
    } else {
      u64 addr = r.is_pcrel ? (hdr.addr + r.offset + addend + 4) : addend;
      Subsection<E> *target = file.find_subsection(ctx, addr);
      if (!target)
        Fatal(ctx) << file << ": bad relocation: " << r.offset;

      rel.target = target;
      rel.is_sym = false;
      rel.addend = addr - target->input_addr;
    }
  }

  return vec;
}

template <>
void Subsection<E>::scan_relocations(Context<E> &ctx) {
  for (Relocation<E> &r : get_rels()) {
    Symbol<E> *sym = r.sym();
    if (!sym || !sym->file)
      continue;

    if (sym->file->is_dylib)
      ((DylibFile<E> *)sym->file)->is_alive = true;

    if ((r.type == X86_64_RELOC_TLV) != sym->is_tlv)
      Error(ctx) << "illegal thread local variable reference to regular symbol `"
                 << *sym << "`";

    switch (r.type) {
    case X86_64_RELOC_BRANCH:
      if (sym->is_imported)
        sym->flags |= NEEDS_STUB;
      break;
    case X86_64_RELOC_GOT:
    case X86_64_RELOC_GOT_LOAD:
      sym->flags |= NEEDS_GOT;
      break;
    case X86_64_RELOC_TLV:
      sym->flags |= NEEDS_THREAD_PTR;
      break;
    }
  }
}

template <>
void Subsection<E>::apply_reloc(Context<E> &ctx, u8 *buf) {
  std::span<Relocation<E>> rels = get_rels();

  for (i64 i = 0; i < rels.size(); i++) {
    Relocation<E> &r = rels[i];

    if (r.sym() && !r.sym()->file) {
      Error(ctx) << "undefined symbol: " << isec->file << ": " << *r.sym();
      continue;
    }

    u8 *loc = buf + r.offset;
    u64 S = r.get_addr(ctx);
    u64 A = r.addend;
    u64 P = get_addr(ctx) + r.offset;
    u64 G = r.sym() ? r.sym()->got_idx * sizeof(Word<E>) : 0;
    u64 GOT = ctx.got.hdr.addr;

    switch (r.type) {
    case X86_64_RELOC_UNSIGNED:
      ASSERT(r.size == 8);

      if (r.sym() && r.sym()->is_imported)
        break;

      if (r.refers_to_tls())
        *(ul64 *)loc = S + A - ctx.tls_begin;
      else
        *(ul64 *)loc = S + A;
      break;
    case X86_64_RELOC_SUBTRACTOR:
      ASSERT(r.size == 4 || r.size == 8);
      i++;
      ASSERT(rels[i].type == X86_64_RELOC_UNSIGNED);
      if (r.size == 4)
        *(ul32 *)loc = rels[i].get_addr(ctx) + rels[i].addend - S;
      else
        *(ul64 *)loc = rels[i].get_addr(ctx) + rels[i].addend - S;
      break;
    case X86_64_RELOC_SIGNED:
    case X86_64_RELOC_SIGNED_1:
    case X86_64_RELOC_SIGNED_2:
    case X86_64_RELOC_SIGNED_4:
      ASSERT(r.size == 4);
      *(ul32 *)loc = S + A - P - 4 - get_reloc_addend(r.type);
      break;
    case X86_64_RELOC_BRANCH:
      ASSERT(r.size == 4);
      *(ul32 *)loc = S + A - P - 4;
      break;
    case X86_64_RELOC_GOT_LOAD:
    case X86_64_RELOC_GOT:
      ASSERT(r.size == 4);
      *(ul32 *)loc = G + GOT + A - P - 4;
      break;
    case X86_64_RELOC_TLV:
      ASSERT(r.size == 4);
      *(ul32 *)loc = r.sym()->get_tlv_addr(ctx) + A - P - 4;
      break;
    default:
      Fatal(ctx) << *isec<< ": unknown reloc: " << (int)r.type;
    }
  }
}

} // namespace mold::macho
