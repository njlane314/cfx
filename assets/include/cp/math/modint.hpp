#pragma once

#include <concepts>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

// Modular integer with a compile-time 32-bit modulus. Arithmetic is O(1), pow
// is O(log exponent), and inverse is O(log modulus). Products use uint64_t, so
// every modulus representable by uint32_t is safe. Division requires coprimality.
namespace cp {

template <std::uint32_t Modulus> class modint {
    static_assert(Modulus >= 2, "modint modulus must be at least two");

  public:
    constexpr modint() = default;

    template <std::integral Integer> constexpr modint(Integer value) : value_(normalize(value)) {}

    [[nodiscard]] static constexpr std::uint32_t modulus() {
        return Modulus;
    }

    [[nodiscard]] constexpr std::uint32_t value() const {
        return value_;
    }

    constexpr modint& operator+=(modint other) {
        const std::uint64_t sum = static_cast<std::uint64_t>(value_) + other.value_;
        value_ = static_cast<std::uint32_t>(sum >= Modulus ? sum - Modulus : sum);
        return *this;
    }

    constexpr modint& operator-=(modint other) {
        if (value_ < other.value_) {
            value_ = static_cast<std::uint32_t>(static_cast<std::uint64_t>(value_) + Modulus -
                                                other.value_);
        } else {
            value_ -= other.value_;
        }
        return *this;
    }

    constexpr modint& operator*=(modint other) {
        value_ =
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(value_) * other.value_ % Modulus);
        return *this;
    }

    constexpr modint& operator/=(modint other) {
        return *this *= other.inverse();
    }

    [[nodiscard]] constexpr modint operator+() const {
        return *this;
    }

    [[nodiscard]] constexpr modint operator-() const {
        return value_ == 0 ? modint{} : raw(Modulus - value_);
    }

    [[nodiscard]] constexpr modint pow(std::uint64_t exponent) const {
        modint base = *this;
        modint result = 1;
        while (exponent > 0) {
            if ((exponent & 1U) != 0) {
                result *= base;
            }
            base *= base;
            exponent >>= 1U;
        }
        return result;
    }

    [[nodiscard]] constexpr modint inverse() const {
        std::int64_t old_remainder = value_;
        std::int64_t remainder = Modulus;
        std::int64_t old_coefficient = 1;
        std::int64_t coefficient = 0;

        while (remainder != 0) {
            const std::int64_t quotient = old_remainder / remainder;

            const std::int64_t next_remainder = old_remainder - quotient * remainder;
            old_remainder = remainder;
            remainder = next_remainder;

            const std::int64_t next_coefficient = old_coefficient - quotient * coefficient;
            old_coefficient = coefficient;
            coefficient = next_coefficient;
        }

        if (old_remainder != 1) {
            throw std::domain_error("modint value has no multiplicative inverse");
        }
        old_coefficient %= static_cast<std::int64_t>(Modulus);
        if (old_coefficient < 0) {
            old_coefficient += Modulus;
        }
        return raw(static_cast<std::uint32_t>(old_coefficient));
    }

    friend constexpr bool operator==(modint, modint) = default;

    friend constexpr modint operator+(modint left, modint right) {
        return left += right;
    }

    friend constexpr modint operator-(modint left, modint right) {
        return left -= right;
    }

    friend constexpr modint operator*(modint left, modint right) {
        return left *= right;
    }

    friend constexpr modint operator/(modint left, modint right) {
        return left /= right;
    }

  private:
    std::uint32_t value_ = 0;

    static constexpr modint raw(std::uint32_t value) {
        modint result;
        result.value_ = value;
        return result;
    }

    template <std::integral Integer> static constexpr std::uint32_t normalize(Integer value) {
        if constexpr (std::is_signed_v<Integer>) {
            std::intmax_t remainder =
                static_cast<std::intmax_t>(value) % static_cast<std::intmax_t>(Modulus);
            if (remainder < 0) {
                remainder += Modulus;
            }
            return static_cast<std::uint32_t>(remainder);
        } else {
            return static_cast<std::uint32_t>(static_cast<std::uintmax_t>(value) %
                                              static_cast<std::uintmax_t>(Modulus));
        }
    }
};

} // namespace cp
