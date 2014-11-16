# Copyright 2013-2014 the openage authors. See copying.md for legal info.

# code for generating data files and their corresponding structs

from . import util
from .util import dbg
from collections import OrderedDict
import pprint
import re
from string import Template
import struct
import os.path

ENDIANNESS = "<"

#global member type modifiers
READ          = util.NamedObject("binary-read_member")
READ_EXPORT   = util.NamedObject("binary-read-export_member")
NOREAD_EXPORT = util.NamedObject("noread_export_member")
READ_UNKNOWN  = util.NamedObject("read_unknown_member")

#regex for matching type array definitions like int[1337]
#group 1: type name, group 2: length
vararray_match = re.compile("([{0}]+) *\\[([{0}]+)\\] *;?".format("a-zA-Z0-9_"))

#match a simple number
integer_match = re.compile("\\d+")


def encode_value(val):
    """
    encodes val to a (possibly escaped) string,
    for use in a csv column of type valtype (string)
    """

    val = str(val)
    val = val.replace("\\", "\\\\")
    val = val.replace(",", "\\,")
    val = val.replace("\n", "\\n")

    return val


def gather_data(obj, members):
    """
    queries the given object for the given member variables
    and returns that as a dict.

    key: member name
    value: obj's member value
    """
    ret = dict()

    for attr, _ in members:
        ret[attr] = getattr(obj, attr)

    return ret


class Exportable:
    """
    superclass for all exportable data members

    exportable classes inherit from this.
    """

    def __init__(self, **args):
        #store passed arguments as members
        self.__dict__.update(args)

    def dump(self, filename):
        """
        main data dumping function, the magic happens in here.

        recursively dumps all object members as DataDefinitions.

        returns [DataDefinition, ..]
        """

        ret = list()        #returned list of data definitions
        self_data = dict()  #data of the current object

        members = self.get_data_format(allowed_modes=(True, READ_EXPORT, NOREAD_EXPORT), flatten_includes=True)
        for is_parent, export, member_name, member_type in members:

            #gather data members of the currently queried object
            self_data[member_name] = getattr(self, member_name)

            if isinstance(member_type, MultisubtypeMember):
                dbg(lazymsg=lambda: "%s => entering member %s" % (filename, member_name), lvl=3)

                current_member_filename = "%s-%s" % (filename, member_name)

                if isinstance(member_type, SubdataMember):
                    is_single_subdata  = True
                    subdata_item_iter  = self_data[member_name]

                    #filename for the file containing the single subdata type entries:
                    submember_filename = current_member_filename

                else:
                    is_single_subdata  = False

                multisubtype_ref_file_data = list()  #file names for ref types
                subdata_definitions = list()         #subdata member DataDefitions
                for subtype_name, submember_class in member_type.class_lookup.items():
                    #if we are in a subdata member, this for loop will only run through once.
                    #else, do the actions for each subtype

                    if not is_single_subdata:
                        dbg(lazymsg=lambda: "%s => entering multisubtype member %s" % (filename, subtype_name), lvl=3)

                        #iterate over the data for the current subtype
                        subdata_item_iter  = self_data[member_name][subtype_name]

                        #filename for the file containing one of the subtype data entries:
                        submember_filename = "%s/%s" % (filename, subtype_name)

                    submember_data = list()
                    for idx, submember_data_item in enumerate(subdata_item_iter):
                        if not isinstance(submember_data_item, Exportable):
                            raise Exception("tried to dump object not inheriting from Exportable")

                        #generate output filename for next-level files
                        nextlevel_filename = "%s/%04d" % (submember_filename, idx)

                        #recursive call, fetches DataDefinitions and the next-level data dict
                        data_sets, data = submember_data_item.dump(nextlevel_filename)

                        #store recursively generated DataDefinitions to the flat list
                        ret += data_sets

                        #append the next-level entry to the list
                        #that will contain the data for the current level DataDefinition
                        if len(data.keys()) > 0:
                            submember_data.append(data)

                    #always create a file, even with 0 entries.
                    if True:  #old: len(submember_data) > 0:
                        #create DataDefinition for the next-level data pile.
                        subdata_definition = DataDefinition(
                            submember_class,
                            submember_data,
                            submember_filename,
                        )

                        if not is_single_subdata:
                            #create entry for type file index.
                            #for each subtype, create entry in the subtype data file lookup file
                            #sync this with MultisubtypeBaseFile!
                            multisubtype_ref_file_data.append({
                                MultisubtypeMember.MultisubtypeBaseFile.data_format[0][1]: subtype_name,
                                MultisubtypeMember.MultisubtypeBaseFile.data_format[1][1]: "%s%s" % (
                                    subdata_definition.name_data_file, GeneratedFile.output_preferences["csv"]["file_suffix"]
                                ),
                            })

                        subdata_definitions.append(subdata_definition)
                    else:
                        pass

                    if not is_single_subdata:
                        dbg(lazymsg=lambda: "%s => leaving multisubtype member %s" % (filename, subtype_name), lvl=3)

                #store filename instead of data list
                #is used to determine the file to read next.
                # -> multisubtype members: type file index
                # -> subdata members:      filename of subdata
                self_data[member_name] = current_member_filename

                #for multisubtype members, append data definition for storing references to all the subtype files
                if not is_single_subdata and len(multisubtype_ref_file_data) > 0:

                    #this is the type file index.
                    multisubtype_ref_file = DataDefinition(
                        MultisubtypeMember.MultisubtypeBaseFile,
                        multisubtype_ref_file_data,
                        self_data[member_name],                          #create file to contain refs to subtype files
                    )

                    subdata_definitions.append(multisubtype_ref_file)

                #store all created submembers to the flat list
                ret += subdata_definitions

                dbg(lazymsg=lambda: "%s => leaving member %s" % (filename, member_name), lvl=3)


        #return flat list of DataDefinitions and dict of {member_name: member_value, ...}
        return ret, self_data

    def read(self, raw, offset, cls=None, members=None):
        """
        recursively read defined binary data from raw at given offset.

        this is used to fill the python classes with data from the binary input.
        """

        if cls:
            target_class = cls
        else:
            target_class = self

        dbg(lazymsg=lambda: "-> 0x%08x => reading %s" % (offset, repr(cls)), lvl=3)

        #break out of the current reading loop when members don't exist in source data file
        stop_reading_members = False

        if not members:
            members = target_class.get_data_format(allowed_modes=(True, READ_EXPORT, READ, READ_UNKNOWN), flatten_includes=False)

        for is_parent, export, var_name, var_type in members:

            if stop_reading_members:
                if isinstance(var_type, DataMember):
                    replacement_value = var_type.get_empty_value()
                else:
                    replacement_value = 0

                setattr(self, var_name, replacement_value)
                continue

            if isinstance(var_type, GroupMember):
                if not issubclass(var_type.cls, Exportable):
                    raise Exception("class where members should be included is not exportable: %s" % var_type.cls.__name__)

                if isinstance(var_type, IncludeMembers):
                    #call the read function of the referenced class (cls),
                    #but store the data to the current object (self).
                    offset = var_type.cls.read(self, raw, offset, cls=var_type.cls)
                else:
                    #create new instance of referenced class (cls),
                    #use its read method to store data to itself,
                    #then save the result as a reference named `var_name`
                    #TODO: constructor argument passing may be required here.
                    grouped_data = var_type.cls()
                    offset = grouped_data.read(raw, offset)

                    setattr(self, var_name, grouped_data)

            elif isinstance(var_type, MultisubtypeMember):
                #subdata reference implies recursive call for reading the binary data

                #arguments passed to the next-level constructor.
                varargs = dict()

                if var_type.passed_args:
                    if type(var_type.passed_args) == str:
                        var_type.passed_args = set(var_type.passed_args)
                    for passed_member_name in var_type.passed_args:
                        varargs[passed_member_name] = getattr(self, passed_member_name)

                #subdata list length has to be defined beforehand as a object member OR number.
                #it's name or count is specified at the subdata member definition by length.
                list_len = var_type.get_length(self)

                #prepare result storage lists
                if isinstance(var_type, SubdataMember):
                    #single-subtype child data list
                    setattr(self, var_name, list())
                    single_type_subdata = True
                else:
                    #multi-subtype child data list
                    setattr(self, var_name, util.gen_dict_key2lists(var_type.class_lookup.keys()))
                    single_type_subdata = False

                #check if entries need offset checking
                if var_type.offset_to:
                    offset_lookup = getattr(self, var_type.offset_to[0])
                else:
                    offset_lookup = None

                for i in range(list_len):

                    #if datfile offset == 0, entry has to be skipped.
                    if offset_lookup:
                        if not var_type.offset_to[1](offset_lookup[i]):
                            continue
                        #TODO: don't read sequentially, use the lookup as new offset?

                    if single_type_subdata:
                        #append single data entry to the subdata object list
                        new_data_class = var_type.class_lookup[None]
                    else:
                        #to determine the subtype class, read the binary definition
                        #this utilizes an on-the-fly definition of the data to be read.
                        offset = self.read(
                            raw, offset, cls=target_class,
                            members=(((False,) + var_type.subtype_definition),)
                        )

                        #read the variable set by the above read call to
                        #use the read data to determine the denominaton of the member type
                        subtype_name = getattr(self, var_type.subtype_definition[1])

                        #look up the type name to get the subtype class
                        new_data_class = var_type.class_lookup[subtype_name]

                    if not issubclass(new_data_class, Exportable):
                        raise Exception("dumped data is not exportable: %s" % new_data_class.__name__)

                    #create instance of submember class
                    new_data = new_data_class(**varargs)

                    #dbg(lazymsg=lambda: "%s: calling read of %s..." % (repr(self), repr(new_data)), lvl=4)

                    #recursive call, read the subdata.
                    offset = new_data.read(raw, offset, new_data_class)

                    #append the new data to the appropriate list
                    if single_type_subdata:
                        getattr(self, var_name).append(new_data)
                    else:
                        getattr(self, var_name)[subtype_name].append(new_data)

            else:
                #reading binary data, as this member is no reference but actual content.

                data_count = 1
                is_custom_member = False

                if type(var_type) == str:
                    #TODO: generate and save member type on the fly
                    #instead of just reading
                    is_array = vararray_match.match(var_type)

                    if is_array:
                        struct_type = is_array.group(1)
                        data_count  = is_array.group(2)
                        if struct_type == "char":
                            struct_type = "char[]"

                        if integer_match.match(data_count):
                            #integer length
                            data_count = int(data_count)
                        else:
                            #dynamic length specified by member name
                            data_count = getattr(self, data_count)

                    else:
                        struct_type = var_type
                        data_count  = 1

                elif isinstance(var_type, DataMember):
                    #special type requires having set the raw data type
                    struct_type = var_type.raw_type
                    data_count  = var_type.get_length(self)
                    is_custom_member = True

                else:
                    raise Exception("unknown data member definition %s for member '%s'" % (var_type, var_name))

                if data_count < 0:
                    raise Exception("invalid length %d < 0 in %s for member '%s'" % (data_count, var_type, var_name))

                if struct_type not in util.struct_type_lookup:
                    raise Exception("%s: member %s requests unknown data type %s" % (repr(self), var_name, struct_type))

                if export == READ_UNKNOWN:
                    #for unknown variables, generate uid for the unknown memory location
                    var_name = "unknown-0x%08x" % offset

                #lookup c type to python struct scan type
                symbol = util.struct_type_lookup[struct_type]

                #read that stuff!!11
                dbg(lazymsg=lambda: "        @0x%08x: reading %s<%s> as '< %d%s'" % (offset, var_name, var_type, data_count, symbol), lvl=4)

                struct_format = "%s %d%s" % (ENDIANNESS, data_count, symbol)
                result        = struct.unpack_from(struct_format, raw, offset)

                dbg(lazymsg=lambda: "                \_ = %s" % (result, ), lvl=4)

                if is_custom_member:
                    if not var_type.verify_read_data(self, result):
                        raise Exception("invalid data when reading %s at offset %#08x" % (var_name, offset))

                #TODO: move these into a read entry hook/verification method
                if symbol == "s":
                    #stringify char array
                    result = util.zstr(result[0])
                elif data_count == 1:
                    #store first tuple element
                    result = result[0]

                    if symbol == "f":
                        import math
                        if not math.isfinite(result):
                            raise Exception("invalid float when reading %s at offset %#08x" % (var_name, offset))

                #increase the current file position by the size we just read
                offset += struct.calcsize(struct_format)

                #run entry hook for non-primitive members
                if is_custom_member:
                    result = var_type.entry_hook(result)

                    if result == ContinueReadMember.ABORT:
                        #don't go through all other members of this class!
                        stop_reading_members = True


                #store member's data value
                setattr(self, var_name, result)

        dbg(lazymsg=lambda: "<- 0x%08x <= finished %s" % (offset, repr(cls)), lvl=3)
        return offset

    @classmethod
    def structs(cls):
        """
        create struct definitions for this class and its subdata references.
        """

        ret = list()
        self_member_count = 0

        dbg(lazymsg=lambda: "%s: generating structs" % (repr(cls)), lvl=2)

        #acquire all struct members, including the included members
        members = cls.get_data_format(allowed_modes=(True, READ_EXPORT, NOREAD_EXPORT), flatten_includes=False)
        for is_parent, export, member_name, member_type in members:
            self_member_count += 1
            dbg(lazymsg=lambda: "%s: exporting member %s<%s>" % (repr(cls), member_name, member_type), lvl=3)

            if isinstance(member_type, MultisubtypeMember):
                for subtype_name, subtype_class in member_type.class_lookup.items():
                    if not issubclass(subtype_class, Exportable):
                        raise Exception("tried to export structs from non-exportable %s" % subtype_class)
                    ret += subtype_class.structs()

            elif isinstance(member_type, GroupMember):
                dbg("entering group/include member %s of %s" % (member_name, cls), lvl=3)
                if not issubclass(member_type.cls, Exportable):
                    raise Exception("tried to export structs from non-exportable member included class %s" % repr(member_type.cls))
                ret += member_type.cls.structs()

            else:
                continue

        #create struct only when it has members?
        if True or self_member_count > 0:
            new_def = StructDefinition(cls)
            dbg(lazymsg=lambda: "=> %s: created new struct definition: %s" % (repr(cls), str(new_def)), lvl=3)
            ret.append(new_def)

        return ret

    @classmethod
    def get_effective_type(cls):
        return cls.name_struct

    @classmethod
    def get_data_format(cls, allowed_modes=False, flatten_includes=False, is_parent=False):
        for member in cls.data_format:
            export, member_name, member_type = member

            definitively_return_member = False

            if isinstance(member_type, IncludeMembers):
                if flatten_includes:
                    #recursive call
                    yield from member_type.cls.get_data_format(allowed_modes, flatten_includes, is_parent=True)
                    continue

            elif isinstance(member_type, ContinueReadMember):
                definitively_return_member = True

            if allowed_modes:
                if export not in allowed_modes:
                    if not definitively_return_member:
                        continue

            member_entry = (is_parent,) + member
            yield member_entry


class ContentSnippet:
    """
    one part of text for generated files to be saved in "file_name"

    before whole source files can be written, it's content snippets
    have to be ordered according to their dependency chain.

    also, each snipped can have import requirements that have to be
    included in top of the source.
    """

    section_header   = util.NamedObject("header")
    section_body     = util.NamedObject("body")

    def __init__(self, data, file_name, section, orderby=None, reprtxt=None):
        self.data      = data       #snippet content
        self.file_name = file_name  #snippet wants to be saved in this file
        self.typerefs  = set()      #these types are referenced
        self.typedefs  = set()      #these types are defined
        self.includes  = set()      #needed snippets, e.g. headers
        self.section   = section    #place the snippet in this file section
        self.orderby   = orderby    #use this value for ordering snippets
        self.reprtxt   = reprtxt    #representation text

        self.required_snippets = set() #snippets to be positioned before this one

        #snippet content is ready by default.
        #subclasses may require generation.
        self.data_ready = True

    def get_data(self):
        if not self.data_ready:
            self.generate_content()
        return self.data

    def generate_content(self):
        # no generation needed by default
        pass

    def add_required_snippets(self, snippet_list):
        """
        save required snippets for this one by looking at wanted type references

        the available candidates have to be passed as argument
        """

        self.required_snippets |= {s for s in snippet_list if len(self.typerefs & s.typedefs) > 0}

        dbg(lazymsg=lambda: "snippet %s requires %s" % (repr(self), repr(self.required_snippets)), lvl=3)

        resolved_types = set()
        for s in self.required_snippets:
            resolved_types |= (self.typerefs & s.typedefs)

        missing_types  = self.typerefs - resolved_types

        return missing_types

    def get_required_snippets(self, defined=None):
        """
        return all referenced and the snippet itself in the order they
        need to be put in the file.
        """

        #TODO: loop detection
        ret = list()

        dbg(lazymsg=lambda: "required snippets for %s {" % (repr(self)), push=True, lvl=4)

        # sort snippets deterministically by __lt__ function
        for s in sorted(self.required_snippets):
            ret += s.get_required_snippets()

        dbg(pop=True, lvl=4)
        dbg(lazymsg=lambda: "}", lvl=4)

        ret.append(self)
        return ret

    def __hash__(self):
        """
        hash all relevant snippet properties
        """

        return hash((
            self.data,
            self.file_name,
            self.section,
            frozenset(self.typedefs),
            frozenset(self.typerefs),
        ))

    def __lt__(self, other):
        """
        comparison of two snippets for their ordering
        """

        if isinstance(other, type(self)) or isinstance(self, type(other)):
            if not (other.orderby and self.orderby):
                faild = self if other.orderby else other
                raise Exception("%s doesn't have orderby member set" % (repr(faild)))
            else:
                ret = self.orderby < other.orderby
                dbg(lazymsg=lambda: "%s < %s = %s" % (repr(self), repr(other), ret), lvl=4)
                return ret
        else:
            raise TypeError("unorderable types: %s < %s" % (type(self), type(other)))

    def __eq__(self, other):
        """
        equality check for text snippets
        """

        if type(other) != type(self):
            return False

        return (
            self.file_name   == other.file_name
            and self.data    == other.data
            and self.section == other.section
            and self.typedefs == other.typedefs
            and self.typerefs == other.typerefs
        )

    def __repr__(self):
        if self.reprtxt:
            data = " = %s" % self.reprtxt
        elif self.data:
            data = " = %s..." % repr(self.data[:25])
        else:
            data = ""

        return "%s(file=%s)%s" % (self.__class__.__name__, self.file_name, data)

    def __str__(self):
        if self.data_ready:
            return "".join((
                repr(self), ", "
                "data = '", str(self.data), "'"
            ))
        else:
            return "".join((
                repr(self), ": lazy generation pending"
            ))


class StructSnippet(ContentSnippet):
    """
    text snippet for generating C++ structs.

    it can generate all struct members and inheritance annotations.
    """

    struct_base = Template("""${comment}struct ${struct_name}${inheritance} {
$members
};
""")

    def __init__(self, file_name, struct_name, comment=None, parents=None):
        super().__init__(None, file_name, ContentSnippet.section_body, orderby=struct_name)
        self.data_ready = False

        self.struct_name = struct_name
        self.member_list = list()

        self.set_comment(comment)
        self.set_parents(parents)

        self.reprtxt = "struct %s" % (self.struct_name)
        self.typedefs |= { self.struct_name }

    def set_comment(self, comment):
        if comment:
            self.comment = "".join((
                "/**\n * ",
                "\n * ".join(comment.split("\n")),
                "\n */\n",
            ))
        else:
            self.comment = ""

    def set_parents(self, parent_types):
        if parent_types and len(parent_types) > 0:
            self.typerefs |= set(parent_types)
            self.inheritance = " : %s" % (", ".join(parent_types))
        else:
            self.inheritance = ""

    def add_member(self, member):
        self.member_list.append(member)

    def add_members(self, members):
        self.member_list.extend(members)

    def generate_content(self):
        """
        generate C struct snippet (that should be placed in a header).
        it represents the struct definition in C-code.
        """

        self.members = "".join("\t%s\n" % m for m in self.member_list)

        self.data = self.struct_base.substitute(self.__dict__)
        self.data_ready = True

    def __hash__(self):
        return hash((
            self.struct_name,
            frozenset(self.member_list),
            self.data,
            self.file_name,
            self.section,
            frozenset(self.typedefs),
            frozenset(self.typerefs),
        ))

    def __eq__(self, other):
        if type(other) != type(self):
            return False

        return (
            self.struct_name     == other.struct_name
            and self.file_name   == other.file_name
            and self.data        == other.data
            and self.member_list == other.member_list
            and self.section     == other.section
            and self.typedefs    == other.typedefs
            and self.typerefs    == other.typerefs
        )

    def __str__(self):
        if self.data_ready:
            return super().__str__(self)
        else:
            return "".join((
                repr(self), " => members = \n",
                pprint.pformat(self.member_list),
            ))


class HeaderSnippet(ContentSnippet):
    """
    represents an includable C header
    """

    def __init__(self, header_name, file_name=True, is_global=False):
        self.is_global = is_global
        self.name = header_name

        if self.is_global:
            data = "#include <%s>\n" % self.name
        else:
            data = "#include \"%s\"\n" % self.name

        super().__init__(data, file_name, ContentSnippet.section_header, orderby=header_name)

    def get_file_name(self):
        return self.name

    def __hash__(self):
        return hash((self.name, self.is_global))

    def __eq__(self, other):
        return (
            type(self) == type(other)
            and self.name == other.name
            and self.is_global == other.is_global
        )

    def __repr__(self):
        sym0 = "<" if self.is_global else "\""
        sym1 = ">" if self.is_global else "\""
        return "HeaderSnippet(%s%s%s)" % (sym0, self.name, sym1)


def determine_header(for_type):
    """
    returns the includable headers for using the given C type.
    """

    #these headers are needed for the type
    ret = set()

    cstdinth              = HeaderSnippet("stdint.h", is_global=True)
    stringh               = HeaderSnippet("string",   is_global=True)
    cstringh              = HeaderSnippet("cstring",  is_global=True)
    cstdioh               = HeaderSnippet("cstdio",   is_global=True)
    vectorh               = HeaderSnippet("vector",   is_global=True)
    cstddefh              = HeaderSnippet("stddef.h", is_global=True)
    util_strings_h        = HeaderSnippet("../util/strings.h", is_global=False)
    util_file_h           = HeaderSnippet("../util/file.h", is_global=False)
    util_dir_h            = HeaderSnippet("../util/dir.h", is_global=False)
    util_error_h          = HeaderSnippet("../util/error.h", is_global=False)
    log_h                 = HeaderSnippet("../log.h", is_global=False)

    #lookup for type->{header}
    type_map = {
        "int8_t":          { cstdinth },
        "uint8_t":         { cstdinth },
        "int16_t":         { cstdinth },
        "uint16_t":        { cstdinth },
        "int32_t":         { cstdinth },
        "uint32_t":        { cstdinth },
        "int64_t":         { cstdinth },
        "uint64_t":        { cstdinth },
        "std::string":     { stringh  },
        "std::vector":     { vectorh  },
        "strcmp":          { cstringh },
        "strncpy":         { cstringh },
        "strtok_custom":   { util_strings_h },
        "sscanf":          { cstdioh  },
        "size_t":          { cstddefh },
        "float":           set(),
        "int":             set(),
        "read_csv_file":   { util_file_h },
        "subdata":         { util_file_h },
        "engine_dir":      { util_dir_h },
        "engine_error":    { util_error_h },
        "engine_log":      { log_h },
    }

    if for_type in type_map:
        ret |= type_map[for_type]
    else:
        raise Exception("could not determine header for %s" % for_type)

    return ret

def determine_headers(for_types):
    ret = set()
    for t in for_types:
        ret |= determine_header(t)

    return ret


class GeneratedFile:
    """
    represents a writable file that was generated automatically.

    it's filled by many ContentSnippets before its contents are generated.
    """

    namespace = "gamedata"

    @classmethod
    def namespacify(cls, var_type):
        return "%s::%s" % (cls.namespace, var_type)

    #inserted warning message for generated files
    dontedit = """
//do not edit this file, it's autogenerated. all changes will be undone!
//make changes in the convert script and regenerate the files."""

    #default preferences for output modes
    default_preferences = {
        "folder":         "",
        "file_suffix":    "",
        "content_prefix": Template(""),
        "content_suffix": Template(""),
    }


    #override the default preferences with the
    #configuration for all the output formats
    output_preferences = {
        "csv": {
            "folder":      "",
            "file_suffix": ".docx",
        },
        "struct": {
            "file_suffix": ".gen.h",
            "content_prefix": Template("""#ifndef OPENAGE_${header_guard}_GEN_H_
#define OPENAGE_${header_guard}_GEN_H_

${headers}
%s

namespace ${namespace} {

""" % dontedit),
            "content_suffix": Template("""
} //namespace ${namespace}

#endif
"""),
        },
        "structimpl": {
            "file_suffix":    ".gen.cpp",
            "content_prefix": Template("""
${headers}
%s

namespace ${namespace} {

""" % dontedit),
            "content_suffix": Template("} //namespace ${namespace}\n"),
        }
    }


    def __init__(self, file_name, format):
        self.snippets  = set()
        self.typedefs  = set()
        self.typerefs  = set()
        self.file_name = file_name
        self.format    = format
        self.included_typedefs = set()

    def add_snippet(self, snippet, inherit_typedefs=True):
        if not isinstance(snippet, ContentSnippet):
            raise Exception("only ContentSnippets can be added to generated files, tried %s" % type(snippet))

        if not snippet.file_name == self.file_name and snippet.file_name != True:
            raise Exception("only snippets with the same target file_name can be put into the same generated file.")

        if snippet not in (self.snippets):
            self.snippets.add(snippet)

            if inherit_typedefs:
                self.typedefs |= snippet.typedefs
                self.typerefs |= snippet.typerefs
            else:
                self.included_typedefs |= snippet.typedefs

            dbg(lazymsg=lambda: "adding snippet to %s:" % (repr(self)), lvl=2)
            dbg(lazymsg=lambda: " %s"                   % repr(snippet), lvl=2)
            dbg(lazymsg=lambda: " `- typedefs:  %s"     % snippet.typedefs, lvl=3)
            dbg(lazymsg=lambda: " `- typerefs:  %s"     % snippet.typerefs, lvl=3)
            dbg(lazymsg=lambda: " `- includes:  %s {"   % repr(snippet.includes), push="snippet_add", lvl=3)

            #add all included snippets, namely HeaderSnippets for #include lol.h
            for s in snippet.includes:
                self.add_snippet(s, inherit_typedefs=False)

            dbg(pop="snippet_add", lazymsg=lambda: "}", lvl=3)
        else:
            dbg(lazymsg=lambda: "skipping already present snippet %s" % (repr(snippet)), lvl=2)

    def get_include_snippet(self, file_name=True):
        """
        return a snippet with a header entry for this file to be able to include it.
        """

        ret = HeaderSnippet(
            self.file_name + self.output_preferences[self.format]["file_suffix"],
            file_name=file_name,
            is_global=False,
        )

        ret.typedefs |= self.typedefs
        return ret

    def create_xref_headers(self, file_pool):
        """
        discover and add needed header snippets for type references accross files.
        """

        dbg("%s typerefs %s" % (repr(self), repr(self.typerefs)), lvl=3)
        dbg("%s typedefs %s" % (repr(self), repr(self.typedefs)), lvl=3)

        new_resolves = set()
        for include_candidate in file_pool:
            candidate_resolves = include_candidate.typedefs & (self.typerefs - self.typedefs)

            if len(candidate_resolves) > 0:
                new_header = include_candidate.get_include_snippet()

                dbg(lazymsg=lambda: "%s: to resolve %s" % (repr(self), candidate_resolves), push="add_header", lvl=3)
                self.add_snippet(new_header, inherit_typedefs=False)
                dbg(pop="add_header")

                new_resolves |= candidate_resolves

        still_missing = ((self.typerefs - self.typedefs) - self.included_typedefs) - new_resolves
        if len(still_missing) > 0:
            raise Exception("still missing types for %s:\n%s" % (self, still_missing))

    def create_forward_declarations(self, file_pool):
        """
        create forward declarations for this generated file.

        a forward declatation is needed when a referenced type is defined
        in an included header, that includes a header that includes the first one.
        """

        pass

    def generate(self):
        """
        actually generate the content for this file.
        """

        #TODO: create new snippets for resolving cyclic dependencies (forward declarations)

        dbg(push="generation", lvl=2)

        dbg(lazymsg=lambda: "".join((
            "\n=========== generating %s\n" % (repr(self)),
            "content snippets stored to be inserted:\n",
            pprint.pformat(self.snippets),
            "\n-----------",
        )), lvl=3)

        #apply preference overrides
        prefs = self.default_preferences.copy()
        prefs.update(self.output_preferences[self.format])

        snippets_header = {s for s in self.snippets if s.section == ContentSnippet.section_header}
        snippets_body   = self.snippets - snippets_header

        if len(snippets_body) == 0:
            raise Exception("generated file %s has no body snippets!" % (repr(self)))

        #type references in this file that could not be resolved
        missing_types = set()

        #put snippets into list in correct order
        #snippets will be written according to this [(snippet, prio), ...] list
        snippets_priorized = list()

        #determine each snippet's priority by number of type references and definitions
        #smaller prio means written earlier in the file.
        #also, find snippet dependencies
        dbg("assigning snippet priorities:", push="snippetprio", lvl=4)
        for s in sorted(snippets_body):
            snippet_prio = len(s.typerefs) - len(s.typedefs)
            snippets_priorized.append((s, snippet_prio))
            dbg(lazymsg=lambda: "prio %3.d => %s" % (snippet_prio, repr(s)), lvl=4)

            #let each snippet find others as dependencies
            missing_types |= s.add_required_snippets(self.snippets)

        dbg(pop="snippetprio")

        if len(missing_types) > 0:
            raise Exception("missing types for %s:\n%s" % (repr(self), pprint.pformat(missing_types)))

        #sort snippets according to their priority determined above
        snippets_priorized_sorted = sorted(snippets_priorized, key=lambda s: s[1])

        #create list of snippets to be put in the generated file.
        #[(snippet, prio)]
        snippets_body_sorted = list()
        snippets_body_set = set()

        #fetch list of all required snippets for all snippets to put in the file
        for snippet, prio in snippets_priorized_sorted:
            snippet_candidates = snippet.get_required_snippets()

            dbg(lazymsg=lambda: "required dependency snippet candidates: %s" % (pprint.pformat(snippet_candidates)), lvl=3)
            for s in snippet_candidates:
                if s.section == ContentSnippet.section_header:
                    if s not in snippets_header:
                        dbg(lazymsg=lambda: " `-> ADD  header snippet %s" % (repr(s)), lvl=4)
                        snippets_header.add(s)
                        continue

                elif s.section == ContentSnippet.section_body:
                    if s not in snippets_body_set:
                        snippets_body_sorted.append(s)
                        snippets_body_set.add(s)
                        dbg(lazymsg=lambda: " `-> ADD  body snippet %s" % (repr(s)), lvl=4)
                        continue

                dbg(lazymsg=lambda: " `-> SKIP snippet %s" % (repr(s)), lvl=4)


        #these snippets will be written outside the namespace
        #in the #include section
        snippets_header_sorted = sorted(snippets_header, key=lambda h: (not h.is_global, h.name))

        dbg(lazymsg=lambda: "".join((
            "\n-----------\n",
            "snippets after ordering for %s:\n" % (repr(self)),
            pprint.pformat(snippets_header_sorted + snippets_body_sorted),
            "\n===========",
        )), lvl=3)

        #merge file contents
        header_data = "".join(header.get_data() for header in snippets_header_sorted)
        file_data   = "\n".join(snippet.get_data() for snippet in snippets_body_sorted)

        namespace    = self.namespace
        header_guard = "".join((namespace.upper(), "_", self.file_name.replace("/", "_").upper()))

        #fill file header and footer with the generated file_name
        content_prefix = prefs["content_prefix"].substitute(header_guard=header_guard, namespace=namespace, headers=header_data)
        content_suffix = prefs["content_suffix"].substitute(header_guard=header_guard, namespace=namespace)

        #this is the final file content
        file_data = "".join((content_prefix, file_data, content_suffix))

        #determine output file name
        output_file_name_parts = [
            prefs["folder"],
            "%s%s" % (self.file_name, prefs["file_suffix"])
        ]

        dbg(pop="generation")

        #whee, return the (file_name, content)
        return (os.path.join(*output_file_name_parts), file_data)

    def __repr__(self):
        return "GeneratedFile<%s>(file_name=%s)" % (self.format, self.file_name)


class DataMember:
    """
    member variable of data files and generated structs.

    equals:
    * one column in a csv file.
    * member in the C struct
    * data field in the .dat file
    """

    def __init__(self):
        self.length = 1
        self.raw_type = None
        self.do_raw_read = True

    def get_parsers(self, idx, member):
        raise NotImplementedError("implement the parser generation for the member type %s" % type(self))

    def get_headers(self, output_target):
        raise NotImplementedError("return needed headers for %s for a given output target" % type(self))

    def get_typerefs(self):
        """
        this member entry references these types.
        this is most likely the type name of the corresponding struct entry.
        """

        return set()

    def entry_hook(self, data):
        """
        allows the data member class to modify the input data

        is used e.g. for the number => enum lookup
        """

        return data

    def get_effective_type(self):
        raise NotImplementedError("return the effective (struct) type of member %s" % type(self))

    def get_empty_value(self):
        """
        when this data field is not filled, use the retured value instead.
        """
        return 0

    def get_length(self, obj=None):
        return self.length

    def verify_read_data(self, obj, data):
        """
        gets called for each entry. used to check for storage validity (e.g. 0 expected)
        """
        return True

    def get_struct_entries(self, member_name):
        """
        return the lines to put inside the C struct.
        """

        return [ "%s %s;" % (self.get_effective_type(), member_name) ]

    def __repr__(self):
        raise NotImplementedError("return short description of the member type %s" % (type(self)))


class GroupMember(DataMember):
    """
    member that references to another class, pretty much like the SubdataMember,
    but with a fixed length of 1.

    this is just a reference to a single struct instance.
    """

    def __init__(self, cls):
        super().__init__()
        self.cls = cls

    def get_headers(self, output_target):
        return { self.cls.name_struct_file }

    def get_typerefs(self):
        return { self.get_effective_type() }

    def get_effective_type(self):
        return self.cls.get_effective_type()

    def get_parsers(self, idx, member):
        #TODO: new type of csv file, probably go for yaml...
        return [
            EntryParser(
                [ "this->%s.fill(buf[%d]);" % (member, idx) ],
                headers     = set(),
                typerefs    = set(),
                destination = "fill",
            )
        ]

    def __repr__(self):
        return "GroupMember<%s>" % repr(self.cls)


class IncludeMembers(GroupMember):
    """
    a member that requires evaluating the given class
    as a include first.

    example:
    the unit class "building" and "movable" will both have
    common members that have to be read first.
    """

    def __init__(self, cls):
        super().__init__(cls)

    def get_parsers(self):
        raise Exception("this should never be called!")

    def __repr__(self):
        return "IncludeMember<%s>" % repr(self.cls)


class DynLengthMember(DataMember):
    """
    a member that can have a dynamic length.
    """

    any_length = util.NamedObject("any_length")

    def __init__(self, length):

        type_ok = False

        if (type(length) in (int, str)) or (length is self.any_length):
            type_ok = True

        if callable(length):
            type_ok = True

        if not type_ok:
            raise Exception("invalid length type passed to %s: %s<%s>" % (type(self), length, type(length)))

        self.length = length

    def get_length(self, obj=None):
        if self.is_dynamic_length():
            if self.length is self.any_length:
                return self.any_length

            if not obj:
                raise Exception("dynamic length query requires source object")

            if callable(self.length):
                #length is a lambda that determines the length by some fancy manner
                #pass the target object as lambda parameter.
                length_def = self.length(obj)

                #if the lambda returns a non-dynamic length (aka a number)
                #return it directly. otherwise, the returned variable name
                #has to be looked up again.
                if not self.is_dynamic_length(target=length_def):
                    return length_def

            else:
                #self.length specifies the attribute name where the length is stored
                length_def = self.length

            #look up the given member name and return the value.
            if not isinstance(length_def, str):
                raise Exception("length lookup definition is not str: %s<%s>" % (length_def, type(length_def)))

            return getattr(obj, length_def)

        else:
            #non-dynamic length (aka plain number) gets returned directly
            return self.length

    def is_dynamic_length(self, target=None):
        if target == None:
            target = self.length

        if target is self.any_length:
            return True
        elif isinstance(target, str):
            return True
        elif isinstance(target, int):
            return False
        elif callable(target):
            return True
        else:
            raise Exception("unknown length definition supplied: %s" % target)


class RefMember(DataMember):
    """
    a struct member that can be referenced/references another struct.
    """

    def __init__(self, type_name, file_name):
        DataMember.__init__(self)
        self.type_name = type_name
        self.file_name = file_name

        #xrefs not supported yet.
        #would allow reusing a struct definition that lies in another file
        self.resolved  = False


class NumberMember(DataMember):
    """
    this struct member/data column contains simple numbers
    """

    #primitive types, directly parsable by sscanf
    type_scan_lookup = {
        "char":          "hhd",
        "int8_t":        "hhd",
        "uint8_t":       "hhu",
        "int16_t":       "hd",
        "uint16_t":      "hu",
        "int":           "d",
        "int32_t":       "d",
        "uint":          "u",
        "uint32_t":      "u",
        "float":         "f",
    }

    def __init__(self, number_def):
        super().__init__()
        if number_def not in self.type_scan_lookup:
            raise Exception("created number column from unknown type %s" % number_def)

        self.number_type = number_def
        self.raw_type    = number_def

    def get_parsers(self, idx, member):
        scan_symbol = self.type_scan_lookup[self.number_type]

        return [
            EntryParser(
                [ "if (sscanf(buf[%d], \"%%%s\", &this->%s) != 1) { return %d; }" % (idx, scan_symbol, member, idx) ],
                headers     = determine_header("sscanf"),
                typerefs    = set(),
                destination = "fill",
            )
        ]

    def get_headers(self, output_target):
        if "struct" == output_target:
            return determine_header(self.number_type)
        else:
            return set()

    def get_effective_type(self):
        return self.number_type

    def __repr__(self):
        return self.number_type


#TODO: convert to KnownValueMember
class ZeroMember(NumberMember):
    """
    data field that is known to always needs to be zero.
    neat for finding offset errors.
    """

    def __init__(self, raw_type, length=1):
        super().__init__(raw_type)
        self.length = length

    def verify_read_data(self, obj, data):
        #fail if a single value of data != 0
        if any(False if v == 0 else True for v in data):
            return False
        else:
            return True


class ContinueReadMember(NumberMember):
    """
    data field that aborts reading further members of the class
    when its value == 0.
    """

    ABORT    = util.NamedObject("data_absent")
    CONTINUE = util.NamedObject("data_exists")

    def __init__(self, raw_type):
        super().__init__(raw_type)

    def entry_hook(self, data):
        if data == 0:
            return self.ABORT
        else:
            return self.CONTINUE

    def get_empty_value(self):
        return 0

    def get_parsers(self, idx, member):
        entry_parser_txt = (
            "//remember if the following members are undefined",
            "if (0 == strcmp(buf[%d], \"%s\")) {" % (idx, repr(self.ABORT)),
            "\tthis->%s = 0;" % (member),
            "} else if (0 == strcmp(buf[%d], \"%s\")) {" % (idx, repr(self.CONTINUE)),
            "\tthis->%s = 1;" % (member),
            "} else {",
            "\tthrow openage::util::Error(\"unexpected value '%%s' for %s\", buf[%d]);" % (self.__class__.__name__, idx),
            "}",
        )

        return [
            EntryParser(
                entry_parser_txt,
                headers     = determine_headers(("strcmp", "engine_error")),
                typerefs    = set(),
                destination = "fill",
            )
        ]


class EnumMember(RefMember):
    """
    this struct member/data column is a enum.
    """

    def __init__(self, type_name, values, file_name=None):
        super().__init__(type_name, file_name)
        self.values    = values
        self.resolved  = True    #TODO, xrefs not supported yet.

    def get_parsers(self, idx, member):
        enum_parse_else = ""
        enum_parser = list()
        enum_parser.append("// parse enum %s" % (self.type_name))
        for enum_value in self.values:
            enum_parser.extend([
                "%sif (0 == strcmp(buf[%d], \"%s\")) {" % (enum_parse_else, idx, enum_value),
                "\tthis->%s = %s::%s;"                  % (member, self.type_name, enum_value),
                "}",
            ])
            enum_parse_else = "else "

        enum_parser.extend([
            "else {",
            "\tthrow openage::util::Error(\"unknown enum value '%%s' encountered. valid are: %s\\n---\\nIf this is an inconsistency due to updates in the media converter, `make media` should fix it\\n---\", buf[%d]);" % (",".join(self.values), idx),
            "}",
        ])

        return [
            EntryParser(
                enum_parser,
                headers     = determine_headers(("strcmp", "engine_error")),
                typerefs    = set(),
                destination = "fill",
            )
        ]

    def get_headers(self, output_target):
        return set()

    def get_typerefs(self):
        return { self.get_effective_type() }

    def get_effective_type(self):
        return self.type_name

    def validate_value(self, value):
        return value in self.values

    def get_snippets(self, file_name, format):
        """
        generate enum snippets from given data

        input: EnumMember
        output: ContentSnippet
        """

        if format == "struct":
            snippet_file_name = self.file_name or file_name

            txt = list()

            #create enum definition
            txt.extend([
                "enum class %s {\n\t" % self.type_name,
                ",\n\t".join(self.values),
                "\n};\n\n",
            ])

            snippet = ContentSnippet(
                "".join(txt),
                snippet_file_name,
                ContentSnippet.section_body,
                orderby=self.type_name,
                reprtxt="enum class %s" % self.type_name,
            )
            snippet.typedefs |= { self.type_name }

            return [ snippet ]
        else:
            return list()

    def __repr__(self):
        return "enum %s" % self.type_name


class EnumLookupMember(EnumMember):
    """
    enum definition, does lookup of raw datfile data => enum value
    """

    def __init__(self, type_name, lookup_dict, raw_type, file_name=None):
        super().__init__(
            type_name,
            [v for k,v in sorted(lookup_dict.items())],
            file_name
        )
        self.lookup_dict = lookup_dict
        self.raw_type = raw_type

    def entry_hook(self, data):
        """
        perform lookup of raw data -> enum member name
        """

        try:
            return self.lookup_dict[data]
        except KeyError as e:
            try:
                h = " = %s" % hex(data)
            except TypeError:
                h = ""
            raise Exception("failed to find %s%s in lookup dict %s!" % (str(data), h, self.type_name)) from None


class CharArrayMember(DynLengthMember):
    """
    struct member/column type that allows to store equal-length char[n].
    """

    def __init__(self, length):
        super().__init__(length)
        self.raw_type = "char[]"

    def get_parsers(self, idx, member):
        headers = set()

        if self.is_dynamic_length():
            lines = [ "this->%s = buf[%d];" % (member, idx) ]
        else:
            data_length = self.get_length()
            lines = [
                "strncpy(this->%s, buf[%d], %d); this->%s[%d] = '\\0';" % (
                    member, idx, data_length, member, data_length-1
                )
            ]
            headers |= determine_header("strncpy")

        return [
            EntryParser(
                lines,
                headers     = headers,
                typerefs    = set(),
                destination = "fill",
            )
        ]


    def get_headers(self, output_target):
        ret = set()

        if "struct" == output_target:
            if self.is_dynamic_length():
                ret |= determine_header("std::string")

        return ret

    def get_effective_type(self):
        if self.is_dynamic_length():
            return "std::string"
        else:
            return "char";

    def get_empty_value(self):
        return ""

    def __repr__(self):
        return "%s[%s]" % (self.get_effective_type(), self.length)


class StringMember(CharArrayMember):
    """
    member with unspecified string length, aka std::string
    """

    def __init__(self):
        super().__init__(DynLengthMember.any_length)


class MultisubtypeMember(RefMember, DynLengthMember):
    """
    struct member/data column that groups multiple references to
    multiple other data sets.
    """

    class MultisubtypeBaseFile(Exportable):
        """
        class that describes the format
        for the base-file pointing to the per-subtype files.
        """

        name_struct_file   = "util"
        name_struct        = "multisubtype_ref"
        struct_description = "format for multi-subtype references"

        data_format = (
            (NOREAD_EXPORT, "subtype", "std::string"),
            (NOREAD_EXPORT, "filename", "std::string"),
        )


    def __init__(self, type_name, subtype_definition, class_lookup, length, passed_args=None, ref_to=None, offset_to=None, file_name=None, ref_type_params=None):
        RefMember.__init__(self, type_name, file_name)
        DynLengthMember.__init__(self, length)

        self.subtype_definition = subtype_definition  #!< to determine the subtype for each entry, read this value to do the class_lookup
        self.class_lookup       = class_lookup        #!< dict to look up type_name => exportable class
        self.passed_args        = passed_args         #!< list of member names whose values will be passed to the new class
        self.ref_to             = ref_to              #!< add this member name's value to the filename
        self.offset_to          = offset_to           #!< link to member name which is a list of binary file offsets
        self.ref_type_params    = ref_type_params     #!< dict to specify type_name => constructor arguments

        #no xrefs supported yet..
        self.resolved          = True

    def get_headers(self, output_target):
        if "struct" == output_target:
            return determine_header("std::vector")
        elif "structimpl" == output_target:
            return determine_header("read_csv_file")
        else:
            return set()

    def get_effective_type(self):
        return self.type_name

    def get_empty_value(self):
        return list()

    def get_contained_types(self):
        return {
            contained_type.get_effective_type()
            for contained_type in self.class_lookup.values()
        }

    def get_parsers(self, idx, member):
        return [
            EntryParser(
                [ "this->%s.index_file.filename = buf[%d];" % (member, idx) ],
                headers     = set(),
                typerefs    = set(),
                destination = "fill",
            ),
            EntryParser(
                [
                    "this->%s.recurse(basedir);" % (member),
                ],
                headers     = set(),
                typerefs    = set(),
                destination = "recurse",
            )

        ]

    def get_typerefs(self):
        return { self.type_name }

    def get_snippets(self, file_name, format):
        """
        return struct definitions for this type
        """

        snippet_file_name = self.file_name or file_name

        if format == "struct":
            snippet = StructSnippet(snippet_file_name, self.type_name)

            for (entry_name, entry_type) in sorted(self.class_lookup.items()):
                entry_type = entry_type.get_effective_type()
                snippet.add_member(
                    "struct openage::util::subdata<%s> %s;" % (
                        GeneratedFile.namespacify(entry_type), entry_name
                    )
                )
                snippet.typerefs |= {entry_type}

            snippet.includes |= determine_header("subdata")
            snippet.typerefs |= {self.MultisubtypeBaseFile.name_struct}
            snippet.add_member("struct openage::util::subdata<%s> index_file;\n" % (self.MultisubtypeBaseFile.name_struct))

            snippet.add_members((
                "%s;" % m.get_signature()
                for _, m in sorted(DataFormatter.member_methods.items())
            ))

            return [ snippet ]

        elif format == "structimpl":
            #TODO: generalize this member function generation...

            txt = list()
            txt.extend((
                "int %s::fill(char * /*line*/) {\n" % (self.type_name),
                "	return -1;\n",
                "}\n",
            ))

            #function to recursively read the referenced files
            txt.extend((
                "int %s::recurse(openage::util::Dir basedir) {\n" % (self.type_name),
                "	this->index_file.read(basedir); //read ref-file entries\n",
                "	int subtype_count = this->index_file.data.size();\n"
                "	if (subtype_count != %s) {\n" % len(self.class_lookup),
                "		throw openage::util::Error(\"multisubtype index file entry count mismatched! %%d != %d\", subtype_count);\n" % (len(self.class_lookup)),
                "	}\n\n",
                "	openage::util::Dir new_basedir = basedir.append(openage::util::dirname(this->index_file.filename));\n",
                "	int idx = -1, idxtry;\n\n",
                "	//yes, the following code can be heavily optimized and converted to member methods..\n",
            ))

            for (entry_name, entry_type) in sorted(self.class_lookup.items()):
                #get the index_file data index of the current entry first
                txt.extend((
                    "	idxtry = 0;\n",
                    "	for (auto &file_reference : this->index_file.data) {\n",
                    "		if (file_reference.subtype == \"%s\") {\n" % (entry_name),
                    "			\tidx = idxtry;\n",
                    "			break;\n",
                    "		}\n",
                    "		idxtry += 1;\n",
                    "	}\n",
                    "	if (idx == -1) {\n",
                    "		throw openage::util::Error(\"multisubtype index file contains no entry for %s!\");\n" % (entry_name),
                    "	}\n",
                    "	this->%s.filename = this->index_file.data[idx].filename;\n" % (entry_name),
                    "	this->%s.read(new_basedir);\n" % (entry_name),
                    "	idx = -1;\n\n"
                ))
            txt.append("	return -1;\n}\n")

            snippet = ContentSnippet(
                "".join(txt),
                snippet_file_name,
                ContentSnippet.section_body,
                orderby=self.type_name,
                reprtxt="multisubtype %s container fill function" % self.type_name,
            )
            snippet.typerefs |= self.get_contained_types() | {self.type_name, self.MultisubtypeBaseFile.name_struct }
            snippet.includes |= determine_headers(("engine_dir","engine_error"))

            return [ snippet ]

        else:
            return list()

    def __repr__(self):
        return "MultisubtypeMember<%s:len=%s>" % (self.type_name, self.length)


class SubdataMember(MultisubtypeMember):
    """
    struct member/data column that references to one another data set.
    """

    def __init__(self, ref_type, length, offset_to=None, ref_to=None, ref_type_params=None, passed_args=None):
        super().__init__(
            type_name          = None,
            subtype_definition = None,
            class_lookup       = {None: ref_type},
            length             = length,
            offset_to          = offset_to,
            ref_to             = ref_to,
            ref_type_params    = {None: ref_type_params},
            passed_args        = passed_args,
        )

    def get_headers(self, output_target):
        if "struct" == output_target:
            return determine_header("subdata")
        else:
            return set()

    def get_subtype(self):
        return GeneratedFile.namespacify(tuple(self.get_contained_types())[0])

    def get_effective_type(self):
        return "openage::util::subdata<%s>" % (self.get_subtype())

    def get_parsers(self, idx, member):
        return [
            EntryParser(
                [ "this->%s.filename = buf[%d];" % (member, idx) ],
                headers     = set(),
                typerefs    = set(),
                destination = "fill",
            ),
            EntryParser(
                [ "this->%s.read(basedir);" % (member) ],
                headers     = set(),
                typerefs    = set(),
                destination = "recurse",
            ),
        ]

    def get_snippets(self, file_name, format):
        return list()

    def get_typerefs(self):
        return self.get_contained_types()

    def get_subdata_type_name(self):
        return self.class_lookup[None].__name__

    def __repr__(self):
        return "SubdataMember<%s:len=%s>" % (self.get_subdata_type_name(), self.length)


class ArrayMember(SubdataMember):
    """
    autogenerated subdata member for arrays like float[8].
    """

    def __init__(self, ref_type, length, ref_type_params=None):
        super().__init__(ref_type, length)

    def __repr__(self):
        return "ArrayMember<%s:len=%s>" % (self.get_subdata_type_name(), self.length)


class StructDefinition:
    """
    input data read from the data files.

    one data set roughly represents one struct in the gamedata dat file.
    it consists of multiple DataMembers, they define the struct members.
    """

    def __init__(self, target):
        """
        create a struct definition from an Exportable
        """

        dbg("generating struct definition from %s" % (repr(target)), lvl=3)

        self.name_struct_file   = target.name_struct_file    #!< name of file where generated struct will be placed
        self.name_struct        = target.name_struct         #!< name of generated C struct
        self.struct_description = target.struct_description  #!< comment placed above generated C struct
        self.prefix             = None
        self.target             = target                     #!< target Exportable class that defines the data format

        #create ordered dict of member type objects from structure definition
        self.members = OrderedDict()
        self.inherited_members = list()
        self.parent_classes = list()

        target_members = target.get_data_format(allowed_modes=(True, READ_EXPORT, NOREAD_EXPORT), flatten_includes=True)
        for is_parent, export, member_name, member_type in target_members:

            if isinstance(member_type, IncludeMembers):
                raise Exception("something went very wrong, inheritance should be flattened at this point.")

            if type(member_name) is not str:
                raise Exception("member name has to be a string, currently: %s<%s>" % (str(member_name), type(member_name)))

            #create member type class according to the defined member type
            if type(member_type) == str:
                array_match = vararray_match.match(member_type)
                if array_match:
                    array_type   = array_match.group(1)
                    array_length = array_match.group(2)

                    if array_type == "char":
                        member = CharArrayMember(array_length)
                    elif array_type in NumberMember.type_scan_lookup:
                        #member = ArrayMember(ref_type=NumberMember, length=array_length, ref_type_params=[array_type])
                        #BIG BIG TODO
                        raise Exception("TODO: implement exporting arrays!")
                    else:
                        raise Exception("member %s has unknown array type %s" % (member_name, member_type))

                elif member_type == "std::string":
                    member = StringMember()
                else:
                    member = NumberMember(member_type)

            elif isinstance(member_type, DataMember):
                member = member_type

            else:
                raise Exception("unknown member type specification!")

            if member is None:
                raise Exception("member %s of struct %s is None" % (member_name, self.name_struct))

            self.members[member_name] = member

            if is_parent:
                self.inherited_members.append(member_name)

        members = target.get_data_format(flatten_includes=False)
        for _, _, _, member_type in members:
            if isinstance(member_type, IncludeMembers):
                self.parent_classes.append(member_type.cls)

    def dynamic_ref_update(self, lookup_ref_data):
        """
        update ourself the with the given reference data.

        data members can be cross references to definitions somewhere else.
        this symbol resolution is done here by replacing the references.
        """

        for member_name, member_type in self.members.items():
            if not isinstance(member_type, RefMember):
                continue

            #this member of self is already resolved
            if member_type.resolved:
                continue

            type_name = member_type.get_effective_type()

            #replace the xref with the real definition
            self.members[type_name] = lookup_ref_data[type_name]

    def generate_struct(self, genfile):
        """
        generate C struct snippet (that should be placed in a header).
        it represents the struct definition in C-code.
        """

        parents = [parent_class.get_effective_type() for parent_class in self.parent_classes]
        snippet = StructSnippet(self.name_struct_file, self.name_struct, self.struct_description, parents)

        snippet.typedefs.add(self.name_struct)

        #add struct members and inheritance parents
        for member_name, member_type in self.members.items():
            if member_name in self.inherited_members:
                #inherited members don't need to be added as they're stored in the superclass
                continue

            snippet.includes |= member_type.get_headers("struct")
            snippet.typerefs |= member_type.get_typerefs()

            snippet.add_members(member_type.get_struct_entries(member_name))

        #append member count variable
        snippet.add_member("static constexpr size_t member_count = %d;" % len(self.members))
        snippet.includes |= determine_header("size_t")

        #add filling function prototypes
        for memname, m in sorted(genfile.member_methods.items()):
            snippet.add_member("%s;" % m.get_signature())
            snippet.includes |= m.get_headers()

        return [ snippet ]

    def generate_struct_implementation(self, genfile):
        """
        create C code for the implementation of the struct functions.
        it is used to fill a struct instance with data of a csv data line.
        """

        #returned snippets
        ret = list()

        #variables to be replaced in the function template
        template_args = {
            "member_count":  self.name_struct + "::member_count",
            "delimiter":     genfile.DELIMITER,
            "struct_name":   self.name_struct,
        }

        #create a list of lines for each parser
        #a parser converts one csv line to struct entries
        parsers = util.gen_dict_key2lists(genfile.member_methods.keys())

        #place all needed parsers into their requested member function destination
        for idx, (member_name, member_type) in enumerate(self.members.items()):
            for parser in member_type.get_parsers(idx, member_name):
                parsers[parser.destination].append(parser)

        #create parser snippets and return them
        for parser_type, parser_list in parsers.items():
            ret.append(
                genfile.member_methods[parser_type].get_snippet(
                    parser_list,
                    file_name  = self.name_struct_file,
                    class_name = self.name_struct,
                    data       = template_args,
                )
            )

        return ret


    def __str__(self):
        ret = [
            repr(self),
            "\n\tstruct file name: ", self.name_struct_file,
            "\n\tstruct name: ", self.name_struct,
            "\n\tstruct description: ", self.struct_description,
        ]
        return "".join(ret)

    def __repr__(self):
        return "StructDefinition<%s>" % self.name_struct


class DataDefinition(StructDefinition):
    """
    data structure definition by given object including data.
    """

    def __init__(self, target, data, name_data_file):
        super().__init__(
            target,
        )

        self.data               = data                #!< list of dicts, member_name=>member_value
        self.name_data_file     = name_data_file      #!< name of file where data will be placed in

    def generate_csv(self, genfile):
        """
        create a text snippet to represent the csv data
        """

        member_types = self.members.values()
        csv_column_types = list()

        #create column types line entries as comment in the csv file
        for c_type in member_types:
            csv_column_types.append(repr(c_type))

        #the resulting csv content
        #begin with the csv information comment header
        txt = [
            "#struct ", self.name_struct, "\n",
            "".join("#%s\n" % line for line in self.struct_description.split("\n")),
            "#", genfile.DELIMITER.join(csv_column_types), "\n",
            "#", genfile.DELIMITER.join(self.members.keys()), "\n",
        ]

        #create csv data lines:
        for idx, data_line in enumerate(self.data):
            row_entries = list()
            for member_name, member_type in self.members.items():
                entry = data_line[member_name]

                make_relpath = False

                #check if enum data value is valid
                if isinstance(member_type, EnumMember):
                    if not member_type.validate_value(entry):
                        raise Exception("data entry %d '%s' not a valid %s value" % (idx, entry, repr(member_type)))

                #insert filename to read this field
                if isinstance(member_type, MultisubtypeMember):
                    #subdata member stores the follow-up filename
                    entry = "%s%s" % (entry, GeneratedFile.output_preferences["csv"]["file_suffix"])
                    make_relpath = True

                if self.target == MultisubtypeMember.MultisubtypeBaseFile:
                    #if the struct definition target is the multisubtype base file,
                    #it already created the filename entry.
                    #it needs to be made relative as well.
                    if member_name == MultisubtypeMember.MultisubtypeBaseFile.data_format[1][1]:
                        #only make the filename entry relative
                        make_relpath = True

                if make_relpath:
                    #filename to reference to, make it relative to the current file name
                    entry = os.path.relpath(entry, os.path.dirname(self.name_data_file))

                #encode each data field, to escape newlines and commas
                row_entries.append(encode_value(entry))

            #create one csv line, separated by DELIMITER (probably a ,)
            txt.extend((genfile.DELIMITER.join(row_entries), "\n"))

        if self.prefix:
            snippet_file_name = self.prefix + self.name_data_file
        else:
            snippet_file_name = self.name_data_file

        return [ContentSnippet(
            "".join(txt),
            snippet_file_name,
            ContentSnippet.section_body,
            orderby=self.name_struct,
            reprtxt="csv for %s" % self.name_struct,
        )]

    def __str__(self):
        ret = [
            "\n\tdata file name: ", str(self.name_data_file),
            "\n\tdata: ", str(self.data),
        ]
        return "%s%s" % (super().__str__(), "".join(ret))

    def __repr__(self):
        return "DataDefinition<%s>" % self.name_struct


class EntryParser:
    def __init__(self, lines, headers, typerefs, destination="fill"):
        self.lines       = lines
        self.headers     = headers
        self.typerefs    = typerefs
        self.destination = destination

    def get_code(self, indentlevel=1):
        indent = "\t" * indentlevel
        return indent + ("\n" + indent).join(self.lines)


class ParserTemplate:
    def __init__(self, signature, template, impl_headers, headers):
        self.signature    = signature     #!< function signature, containing %s as possible namespace prefix
        self.template     = template      #!< template text where insertions will be made
        self.impl_headers = impl_headers  #!< headers for the c file
        self.headers      = headers       #!< headers for the header file

    def get_signature(self, class_name):
        return self.signature % (class_name)

    def get_text(self, class_name, data):
        data["signature"] = self.get_signature("%s::" % class_name)
        return Template(self.template).substitute(data)


class ParserMemberFunction:
    def __init__(self, templates, func_name):
        self.templates = templates  #!< dict: key=function_member_count (None=any), value=ParserTemplate
        self.func_name = func_name

    def get_template(self, lookup):
        """
        get the appropritate parser member function template
        lookup = len(function members)

        -> this allows to generate stub functions without unused variables.
        """

        if lookup not in self.templates.keys():
            lookup = None
        return self.templates[lookup]

    def get_snippet(self, parser_list, file_name, class_name, data):
        data["parsers"]    = "\n".join(parser.get_code(1) for parser in parser_list)
        data["class_name"] = class_name

        #lookup function for length of parser list.
        #if the len is 0, this should provide the stub function.
        template = self.get_template(len(parser_list))
        snippet = ContentSnippet(
            template.get_text(class_name, data),
            file_name,
            ContentSnippet.section_body,
            orderby="%s_%s" % (class_name, self.func_name),
            reprtxt=template.get_signature(class_name + "::"),
        )

        snippet.includes |= template.impl_headers | set().union(*(p.headers for p in parser_list))
        snippet.typerefs |= { class_name }.union(*(p.typerefs for p in parser_list))

        return snippet

    def get_signature(self):
        """
        return the function signature for this member function.
        """

        return self.templates[None].get_signature("")

    def get_headers(self):
        """
        return the needed headers for the function signature of this member function.
        """

        return self.templates[None].headers


class DataFormatter:
    """
    transforms and merges data structures
    the input data also specifies the output structure.

    this class generates the plaintext being stored in the data files
    it is the central part of the data exporting functionality.
    """

    #csv column delimiter:
    DELIMITER = ","

    member_methods = {
        "fill": ParserMemberFunction(
            func_name = "fill",
            templates = {
                0: ParserTemplate(
                    signature    = "int %sfill(char * /*by_line*/)",
                    headers      = set(),
                    impl_headers = set(),
                    template     = """$signature {
\treturn -1;
}
"""
                ),
                None: ParserTemplate(
                    signature    = "int %sfill(char *by_line)",
                    headers      = set(),
                    impl_headers = determine_headers(("strtok_custom", "engine_error")),
                    template     = """$signature {
\tchar *buf[$member_count];
\tint count = openage::util::string_tokenize_to_buf(by_line, '$delimiter', buf, $member_count);

\tif (count != $member_count) {
\t\tthrow openage::util::Error("tokenizing $struct_name led to %d columns (expecting %zu)!", count, $member_count);
\t}

$parsers

\treturn -1;
}
""",
                ),
            }
        ),
        "recurse": ParserMemberFunction(
            func_name = "recurse",
            templates = {
                0: ParserTemplate(
                    signature    = "int %srecurse(openage::util::Dir /*basedir*/)",
                    headers      = determine_header("engine_dir"),
                    impl_headers = set(),
                    template     = """$signature {
\treturn -1;
}
""",
                ),
                None: ParserTemplate(
                    signature = "int %srecurse(openage::util::Dir basedir)",
                    headers   = determine_header("engine_dir"),
                    impl_headers = set(),
                    template  = """$signature {
$parsers

\treturn -1;
}
""",
                ),
            }
        ),
    }

    def __init__(self):
        #list of all dumpable data sets
        self.data = list()

        #collection of all type definitions
        self.typedefs = dict()

    def add_data(self, data_set_pile, prefix=None):
        """
        add a given StructDefinition to the storage, so it can be exported later.

        other exported data structures are collected from the given input.
        """

        if type(data_set_pile) is not list:
            data_set_pile = [ data_set_pile ]

        #add all data sets
        for data_set in data_set_pile:

            #TODO: allow prefixes for all file types (missing: struct, structimpl)
            if prefix:
                data_set.prefix = prefix

            #collect column type specifications
            for member_name, member_type in data_set.members.items():

                #store resolved (first-time definitions) members in a symbol list
                if isinstance(member_type, RefMember):
                    if member_type.resolved:
                        if member_type.get_effective_type() in self.typedefs:
                            if data_set.members[member_name] is not self.typedefs[member_type.get_effective_type()]:
                                raise Exception("different redefinition of type %s" % member_type.get_effective_type())
                        else:
                            ref_member = data_set.members[member_name]

                            #if not overridden, use name of struct file to store
                            if ref_member.file_name == None:
                                ref_member.file_name = data_set.name_struct_file

                            self.typedefs[member_type.get_effective_type()] = ref_member

            self.data.append(data_set)

    def export(self, requested_formats):
        """
        generate content snippets that will be saved to generated files

        output: {file_name: GeneratedFile, ...}
        """

        #storage of all needed content snippets
        generate_files = list()

        for format in requested_formats:
            files = dict()

            snippets = list()

            #iterate over all stored data sets and
            #generate all data snippets for the requested output formats.
            for data_set in self.data:

                #resolve data xrefs for this data_set
                data_set.dynamic_ref_update(self.typedefs)

                #generate one output chunk list for each requested format
                if format == "csv":
                    new_snippets = data_set.generate_csv(self)

                elif format == "struct":
                    new_snippets = data_set.generate_struct(self)

                elif format == "structimpl":
                    new_snippets = data_set.generate_struct_implementation(self)

                else:
                    raise Exception("unknown export format %s requested" % format)

                snippets.extend(new_snippets)

            #create snippets for the encountered type definitions
            for type_name, type_definition in sorted(self.typedefs.items()):
                dbg(lazymsg=lambda: "getting type definition snippets for %s<%s>.." % (type_name, type_definition), lvl=4)
                type_snippets = type_definition.get_snippets(type_definition.file_name, format)
                dbg(lazymsg=lambda: "`- got %d snippets" % (len(type_snippets)), lvl=4)

                snippets.extend(type_snippets)

            #assign all snippets to generated files
            for snippet in snippets:

                #if this file was not yet created, do it nao
                if snippet.file_name not in files:
                    files[snippet.file_name] = GeneratedFile(snippet.file_name, format)

                files[snippet.file_name].add_snippet(snippet)

            generate_files.extend(files.values())

        #files is currently:
        #[GeneratedFile, ...]

        #find xref header includes
        for gen_file in generate_files:
            #only create headers for non-data files
            if gen_file.format not in ("csv",):
                dbg("%s: creating needed xref headers:" % (repr(gen_file)), push="includegen", lvl=3)
                gen_file.create_xref_headers(generate_files)
                gen_file.create_forward_declarations(generate_files)
                dbg(pop="includegen")

        #actually generate the files
        ret = dict()

        #we now invoke the content generation for each generated file
        for gen_file in generate_files:
            file_name, content = gen_file.generate()
            ret[file_name] = content

        #return {file_name: content, ...}
        return ret
