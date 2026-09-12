module;
#include <cerrno>
export module maylog;
import std;
// We have both the enabled dial, and this pre-processor dial
// The former is intended to change stuff during development, the latter just for limiting release builds really
#ifndef MAYLOG_MIN_LEVEL
#define MAYLOG_MIN_LEVEL 0
#endif
export namespace maylog::config {
struct Rgb {
	std::uint32_t r;
	std::uint32_t g;
	std::uint32_t b;
	constexpr Rgb(std::uint32_t red, std::uint32_t green, std::uint32_t blue) : r(red), g(green), b(blue) {}
};
// https://catppuccin.com/palette/
class CatppuccinFrappe {
public:
	constexpr static Rgb rosewater() { return Rgb(242, 213, 207); }
	constexpr static Rgb flamingo() { return Rgb(238, 190, 190); }
	constexpr static Rgb pink() { return Rgb(244, 184, 228); }
	constexpr static Rgb mauve() { return Rgb(202, 158, 230); }
	constexpr static Rgb red() { return Rgb(231, 130, 132); }
	constexpr static Rgb maroon() { return Rgb(234, 153, 156); }
	constexpr static Rgb peach() { return Rgb(239, 159, 118); }
	constexpr static Rgb yellow() { return Rgb(229, 200, 144); }
	constexpr static Rgb green() { return Rgb(166, 209, 137); }
	constexpr static Rgb teal() { return Rgb(129, 200, 190); }
	constexpr static Rgb sky() { return Rgb(153, 209, 219); }
	constexpr static Rgb sapphire() { return Rgb(133, 193, 220); }
	constexpr static Rgb blue() { return Rgb(140, 170, 238); }
	constexpr static Rgb lavender() { return Rgb(186, 187, 241); }
	constexpr static Rgb text() { return Rgb(198, 208, 245); }
};
// Non-type template parameters (NTTP's) have the following restriction:
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0732r1.pdf
// (ctrl f 17.3.2). Arrays aren't pointers, so we just do that. We could do some
// funky template shenanigans to get is_same working and template the array length
// but fuck that lbr, i'll just waste a few bytes (and have a max)
struct LevelInfo {
	char name[16];
	bool enabled = true;
	Rgb rgb = Rgb(255, 255, 255);
	std::uint32_t level = 0;
	bool is_err = false;
};
struct TagInfo {
	char name[16];
	bool enabled = true;
	Rgb rgb = Rgb(255, 255, 255);
};
}; // namespace maylog::config

using namespace maylog::config;
class Default {
public:
	static constexpr LevelInfo Tmp = LevelInfo("temp", true, CatppuccinFrappe::peach(), 400);
	static constexpr TagInfo TmpT = TagInfo("temp", true, CatppuccinFrappe::peach());
};

export namespace maylog {
enum MaylogFlag : std::uint8_t {
	None = 0b0,
	No = 0b1,
};

constexpr MaylogFlag operator|(MaylogFlag lhs, MaylogFlag rhs) {
	// Use the integer bitwise here, else we'd be recursive
	return static_cast<MaylogFlag>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

template<class... Args>
std::string combo_errno(std::format_string<Args...> format, Args&&... args) {
	std::string message = std::format(format, std::forward<Args>(args)...);
	return std::format("{}: {}", std::move(message), std::strerror(errno));
}

// Level, Tag, Throw are non-type template parameters (co-alesced into Fields now, eg. like we did with the opcode), ie. templated values
// It'd be like a constexpr argument, if we were allowed to do that. Note that they must be structural types
// (https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p2484r0.html#introduction)
// They're wrapped in the auto... Fields so they are optional, and we discover them at consteval, and we can write them in any order
// template<LevelInfo Level = My::TempLvl, TagInfo Tag = My::TempTag, bool Throw = false, typename Lambda>
template<auto... Fields, typename Lambda>
void log(Lambda&& lambda, std::source_location source = std::source_location::current()) {
	constexpr auto [level, tag, flag] = [] consteval {
		struct {
			LevelInfo level = Default::Tmp;
			TagInfo tag = Default::TmpT;
			MaylogFlag flag = None;
		} ret;
		// I would also like a cleaner way of doing this, could do with comp-time reflection but isn't worth the hassle
		bool level_set = false;
		bool tag_set = false;
		bool flag_set = false;
		// https://stackoverflow.com/questions/68872572/lambda-call-operator-and-parenthesized-lambda/68872590…
		([&] {
			using T = decltype(Fields);
			if constexpr (std::is_same_v<LevelInfo, T>) {
				if (level_set)
					throw "Level set more than once";
				level_set = true;
				ret.level = Fields;
			} else if constexpr (std::is_same_v<TagInfo, T>) {
				if (tag_set)
					throw "Tag set more than once";
				tag_set = true;
				ret.tag = Fields;
			} else if constexpr (std::is_same_v<MaylogFlag, T>) {
				if (flag_set)
					throw "Flag set more than once";
				flag_set = true;
				ret.flag = Fields;
			} else {
				static_assert(false, "Invalid template field provided");
			}
		}(),
			...);
		// I would normally use a tuple, but destructing a tuple into constexpr values doesn't seem to work
		// whereas doing it for an aggregate does.
		// (take a big pinch of salt, https://eel.is/c++draft/dcl.struct.bind#7, it was difficult for me to understand)
		// I can guess as to why, but it would just be a guess. If you have a solid understanding, please let me know
		// However, maylog guess from reading the link is that tuples hit the first case (section 7) where they have defined
		// an ADL (https://stackoverflow.com/questions/45437175/c-ordinary-lookup-vs-argument-dependent-lookup) for std::get for std::tuple,
		// so it uses std::get whereas aggregates (ie reg structs) hit section 8 where the compiler just sets the values itself
		return ret;
	}();
	if constexpr (level.enabled and tag.enabled and level.level >= MAYLOG_MIN_LEVEL) {
		std::string message = std::format("[\x1b[38;2;{};{};{}m{}\x1b[0m] [\x1b[38;2;{};{};{}m{}\x1b[0m] [{}:{}] {}",
			level.rgb.r, level.rgb.g, level.rgb.b, level.name,
			tag.rgb.r, tag.rgb.g, tag.rgb.b, tag.name,
			lambda(), source.file_name(), source.line());

		// Append errno
		if constexpr (flag & No) {
			message = std::format("{}: {}", message, std::strerror(errno));
		}

		if constexpr (level.is_err) {
			std::println(std::cerr, "{}", message);
			// cerr is unbuffered by default but just incase + consistency
			std::cerr.flush();
		} else {
			std::println(std::cout, "{}", message);
			std::cout.flush();
		}
	}
}

// Originally, was part of log, but compilers couldn't see far enough for an Ex flag that threw conditionally, so now it's extracted into a segment that always throws
template<auto... Fields, typename Lambda>
[[noreturn]] void fail(Lambda&& lambda, std::source_location source = std::source_location::current()) {
    // Don't manually specify the lambda type, as it will assume the lambda is part of the pack, then try to infer the 2nd lambda type from the parameter
	log<Fields...>(std::move(lambda), source);
	throw std::runtime_error("Failed execution");
}
} // namespace maylog
