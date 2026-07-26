class Cfx < Formula
  desc "Small, auditable, two-command Codeforces workflow"
  homepage "https://github.com/njlane314/cfx"
  license "MIT"
  head "https://github.com/njlane314/cfx.git", branch: "main"

  depends_on "llvm"
  uses_from_macos "curl"

  def install
    system "make", "build", "CXX=#{ENV.cxx}", "CFX_STD=c++20"
    libexec.install ".build/tools/cfx"
    man1.install "release/cfx.1"
    pkgshare.install "assets/include", "assets/templates"
    (pkgshare/"browser").install "assets/browser/extension-id"
    (bin/"cfx").write_env_script libexec/"cfx",
                                 CFX_ASSET_ROOT: pkgshare,
                                 CXX:            formula_opt_bin("llvm")/"clang++"
  end

  test do
    ENV["CFX_STATE_ROOT"] = testpath/"state"
    cases = testpath/"codeforces/4/A/cases"
    cases.mkpath
    (cases/"case-1.in").write "8\n"
    (cases/"case-1.out").write "YES\n"
    (testpath/"codeforces/4/A/solution.cpp").write <<~CPP
      #include "cp/prelude.hpp"
      #include <iostream>
      int main() {
          cp::i64 weight = 0;
          std::cin >> weight;
          std::cout << (weight > 2 && weight % 2 == 0 ? "YES" : "NO") << '\\n';
      }
    CPP
    assert_match "1/1 passed", shell_output("#{bin}/cfx test 4A")
  end
end
