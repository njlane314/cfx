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
    extension_id = (buildpath/"src/browser/extension-id").read.strip
    pkgshare.install "solution.cpp"
    pkgshare.install "src/browser/extension-id"
    (bin/"cfx").write_env_script libexec/"cfx",
                                 CFX_SOLUTION_TEMPLATE:   pkgshare/"solution.cpp",
                                 CFX_CHROME_EXTENSION_ID: extension_id,
                                 CXX:                     formula_opt_bin("llvm")/"clang++"
  end

  test do
    ENV["CFX_STATE_ROOT"] = testpath/"state"
    cases = testpath/"codeforces/4/A/cases"
    cases.mkpath
    (cases/"case-1.in").write "8\n"
    (cases/"case-1.out").write "YES\n"
    (testpath/"codeforces/4/A/solution.cpp").write <<~CPP
      #include <iostream>
      int main() {
          long long weight = 0;
          std::cin >> weight;
          std::cout << (weight > 2 && weight % 2 == 0 ? "YES" : "NO") << '\\n';
      }
    CPP
    assert_match "1/1 passed", shell_output("#{bin}/cfx test 4A")
  end
end
