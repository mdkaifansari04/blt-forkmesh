import 'package:flutter/foundation.dart' show kDebugMode;
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/auth_service.dart';
import '../theme.dart';
import '../widgets/fm_ui.dart';

const _logoAsset = 'assets/images/logo.png';

enum _AuthMode { welcome, login, signup }

class AuthMockFlow extends StatefulWidget {
  const AuthMockFlow({super.key, required this.onAuthenticated});

  final VoidCallback onAuthenticated;

  @override
  State<AuthMockFlow> createState() => _AuthMockFlowState();
}

class _AuthMockFlowState extends State<AuthMockFlow> {
  _AuthMode _mode = _AuthMode.welcome;
  bool _acceptedTerms = false;

  void _show(_AuthMode mode) {
    setState(() => _mode = mode);
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedSwitcher(
      duration: const Duration(milliseconds: 220),
      child: switch (_mode) {
        _AuthMode.welcome => _WelcomeScreen(
          key: const ValueKey('welcome'),
          onLogin: () => _show(_AuthMode.login),
          onSignup: () => _show(_AuthMode.signup),
        ),
        _AuthMode.login => _LoginScreen(
          key: const ValueKey('login'),
          onAuthenticated: widget.onAuthenticated,
          onSignup: () => _show(_AuthMode.signup),
          onBack: () => _show(_AuthMode.welcome),
        ),
        _AuthMode.signup => _SignupScreen(
          key: const ValueKey('signup'),
          acceptedTerms: _acceptedTerms,
          onTermsChanged: (value) =>
              setState(() => _acceptedTerms = value ?? false),
          onAuthenticated: widget.onAuthenticated,
          onLogin: () => _show(_AuthMode.login),
          onBack: () => _show(_AuthMode.welcome),
        ),
      },
    );
  }
}

class _WelcomeScreen extends StatelessWidget {
  const _WelcomeScreen({
    super.key,
    required this.onLogin,
    required this.onSignup,
  });

  final VoidCallback onLogin;
  final VoidCallback onSignup;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: DecoratedBox(
        decoration: const BoxDecoration(
          color: Color(0xFF050505),
          gradient: RadialGradient(
            center: Alignment(-0.65, -0.82),
            radius: 1.2,
            colors: [Color(0xFFEFEAFF), Color(0xFF6F5AD9), Color(0xFF050505)],
            stops: [0, 0.18, 0.48],
          ),
        ),
        child: DecoratedBox(
          decoration: const BoxDecoration(
            gradient: LinearGradient(
              begin: Alignment.topRight,
              end: Alignment.bottomLeft,
              colors: [
                Color(0x00000000),
                Color(0xFF050505),
                Color(0xFF160F35),
                Color(0xFF050505),
              ],
              stops: [0, 0.44, 0.68, 1],
            ),
          ),
          child: SafeArea(
            child: Padding(
              padding: const EdgeInsets.fromLTRB(28, 28, 28, 24),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const _LogoImage(size: 54),
                  const Spacer(),
                  ConstrainedBox(
                    constraints: const BoxConstraints(maxWidth: 460),
                    child: const Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          'Build open, sync easy',
                          style: TextStyle(
                            color: Colors.white,
                            fontSize: 44,
                            height: 0.98,
                            fontWeight: FontWeight.w900,
                            letterSpacing: 0,
                          ),
                        ),
                        SizedBox(height: 18),
                        Text(
                          'Private relay chat, repository mirrors, and signed collaboration from your phone.',
                          style: TextStyle(
                            color: Color(0xFFD8D5DF),
                            fontSize: 17,
                            height: 1.45,
                            fontWeight: FontWeight.w500,
                          ),
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 34),
                  Row(
                    children: [
                      Expanded(
                        child: OutlinedButton(
                          onPressed: onLogin,
                          style: OutlinedButton.styleFrom(
                            foregroundColor: Colors.white,
                            side: const BorderSide(
                              color: Colors.white,
                              width: 1.4,
                            ),
                            padding: const EdgeInsets.symmetric(vertical: 18),
                            shape: const StadiumBorder(),
                          ),
                          child: const Text('Log in'),
                        ),
                      ),
                      const SizedBox(width: 12),
                      Expanded(
                        child: FilledButton(
                          onPressed: onSignup,
                          style: FilledButton.styleFrom(
                            backgroundColor: Colors.white,
                            foregroundColor: Colors.black,
                            padding: const EdgeInsets.symmetric(vertical: 18),
                            shape: const StadiumBorder(),
                          ),
                          child: const Text('Sign up'),
                        ),
                      ),
                    ],
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _LoginScreen extends StatefulWidget {
  const _LoginScreen({
    super.key,
    required this.onAuthenticated,
    required this.onSignup,
    required this.onBack,
  });

  final VoidCallback onAuthenticated;
  final VoidCallback onSignup;
  final VoidCallback onBack;

  @override
  State<_LoginScreen> createState() => _LoginScreenState();
}

class _LoginScreenState extends State<_LoginScreen> {
  final _identifier = TextEditingController();
  final _password = TextEditingController();
  final _totp = TextEditingController();
  bool _loading = false;
  bool _showTotp = false;
  String _hint = '';
  bool _bad = false;

  @override
  void dispose() {
    _identifier.dispose();
    _password.dispose();
    _totp.dispose();
    super.dispose();
  }

  Future<void> _submit() async {
    if (_loading) return;
    final identifier = _identifier.text.trim();
    final password = _password.text;
    if (identifier.isEmpty && password.isEmpty && kDebugMode) {
      // Design-preview path for widget tests and local mock demos only;
      // release builds fall through to the normal validation below so an
      // empty form can never bypass real authentication.
      widget.onAuthenticated();
      return;
    }
    if (identifier.isEmpty || password.isEmpty) {
      setState(() {
        _hint = 'Enter your email or username and password.';
        _bad = true;
      });
      return;
    }
    setState(() {
      _loading = true;
      _hint = '';
      _bad = false;
    });
    try {
      await context.read<AuthService>().login(
        identifier: identifier,
        password: password,
        totp: _totp.text,
      );
      widget.onAuthenticated();
    } on AuthException catch (e) {
      setState(() {
        _showTotp = e.code == 'bad_totp' || _showTotp;
        _hint = e.message;
        _bad = e.code != 'bad_totp';
      });
    } catch (_) {
      setState(() {
        _hint = 'Network error - please try again.';
        _bad = true;
      });
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return _AuthPageScaffold(
      pageTitle: 'Login',
      onBack: widget.onBack,
      child: _AuthContentColumn(
        children: [
          const SizedBox(height: 74),
          const _CenteredLogo(size: 72),
          const SizedBox(height: 28),
          const _AuthTitle(
            title: 'Welcome back',
            subtitle: 'Continue with your ForkMesh account.',
          ),
          const SizedBox(height: 48),
          _AuthField(
            label: 'Email or username',
            controller: _identifier,
            hintText: 'you@example.com',
            textInputAction: TextInputAction.next,
            keyboardType: TextInputType.emailAddress,
          ),
          const SizedBox(height: 24),
          _AuthField(
            label: 'Password',
            controller: _password,
            hintText: 'Your password',
            obscureText: true,
            textInputAction: _showTotp
                ? TextInputAction.next
                : TextInputAction.done,
            onSubmitted: _showTotp ? null : (_) => _submit(),
          ),
          if (_showTotp) ...[
            const SizedBox(height: 24),
            _AuthField(
              label: 'Authenticator code',
              controller: _totp,
              hintText: '123456',
              keyboardType: TextInputType.number,
              textInputAction: TextInputAction.done,
              onSubmitted: (_) => _submit(),
            ),
          ],
          if (_hint.isNotEmpty) ...[
            const SizedBox(height: 18),
            _AuthHint(text: _hint, bad: _bad),
          ],
          const SizedBox(height: 42),
          _PrimaryAuthButton(
            label: _loading ? 'Logging in...' : 'Continue',
            onPressed: _loading ? null : _submit,
          ),
          const SizedBox(height: 18),
          _SecondaryAuthButton(
            label: 'Create an account',
            onPressed: widget.onSignup,
          ),
          const SizedBox(height: 72),
          const _TermsCopy(),
        ],
      ),
    );
  }
}

class _SignupScreen extends StatefulWidget {
  const _SignupScreen({
    super.key,
    required this.acceptedTerms,
    required this.onTermsChanged,
    required this.onAuthenticated,
    required this.onLogin,
    required this.onBack,
  });

  final bool acceptedTerms;
  final ValueChanged<bool?> onTermsChanged;
  final VoidCallback onAuthenticated;
  final VoidCallback onLogin;
  final VoidCallback onBack;

  @override
  State<_SignupScreen> createState() => _SignupScreenState();
}

class _SignupScreenState extends State<_SignupScreen> {
  final _nodeName = TextEditingController();
  final _email = TextEditingController();
  final _password = TextEditingController();
  bool _loading = false;
  String _nameHint =
      'Lowercase letters, numbers and hyphens. Your username is public.';
  String _hint = '';
  bool _nameOk = false;
  bool _bad = false;

  @override
  void dispose() {
    _nodeName.dispose();
    _email.dispose();
    _password.dispose();
    super.dispose();
  }

  Future<void> _checkName() async {
    final auth = context.read<AuthService>();
    final availability = await auth.checkNodeName(_nodeName.text);
    if (!mounted) return;
    setState(() {
      _nodeName.text = _nodeName.text.trim().toLowerCase();
      _nameOk = availability.available;
      _nameHint = availability.message;
    });
  }

  Future<void> _submit() async {
    if (_loading) return;
    final nodeName = _nodeName.text.trim().toLowerCase();
    final email = _email.text.trim();
    final password = _password.text;
    if (!AuthService.nodeNamePattern.hasMatch(nodeName)) {
      setState(() {
        _hint = 'Choose a valid username first.';
        _bad = true;
      });
      return;
    }
    if (!email.contains('@')) {
      setState(() {
        _hint = 'Enter a valid email address.';
        _bad = true;
      });
      return;
    }
    if (password.length < 8) {
      setState(() {
        _hint = 'Password must be at least 8 characters.';
        _bad = true;
      });
      return;
    }
    if (!widget.acceptedTerms) {
      setState(() {
        _hint = 'Accept the Terms and Privacy policy to continue.';
        _bad = true;
      });
      return;
    }
    setState(() {
      _loading = true;
      _hint = '';
      _bad = false;
    });
    try {
      await context.read<AuthService>().signup(
        nodeName: nodeName,
        email: email,
        password: password,
      );
      widget.onAuthenticated();
    } on AuthException catch (e) {
      setState(() {
        _hint = e.message;
        _bad = true;
      });
    } catch (_) {
      setState(() {
        _hint = 'Network error - please try again.';
        _bad = true;
      });
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return _AuthPageScaffold(
      pageTitle: 'Sign up',
      onBack: widget.onBack,
      child: _AuthContentColumn(
        children: [
          const SizedBox(height: 58),
          const _CenteredLogo(size: 78),
          const SizedBox(height: 28),
          const _AuthTitle(
            title: 'Create your account',
            subtitle: 'Set your ForkMesh profile to continue.',
          ),
          const SizedBox(height: 42),
          _AuthField(
            label: 'Username',
            controller: _nodeName,
            hintText: 'ada-lovelace',
            textInputAction: TextInputAction.next,
            onChanged: (_) => _checkName(),
          ),
          const SizedBox(height: 8),
          _AuthHint(
            text: _nameHint,
            bad: !_nameOk && _nodeName.text.isNotEmpty,
          ),
          const SizedBox(height: 20),
          _AuthField(
            label: 'Email',
            controller: _email,
            hintText: 'you@example.com',
            keyboardType: TextInputType.emailAddress,
            textInputAction: TextInputAction.next,
          ),
          const SizedBox(height: 20),
          _AuthField(
            label: 'Password',
            controller: _password,
            hintText: 'At least 8 characters',
            obscureText: true,
            textInputAction: TextInputAction.done,
            onSubmitted: (_) => _submit(),
          ),
          const SizedBox(height: 22),
          Row(
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              Checkbox(
                value: widget.acceptedTerms,
                onChanged: widget.onTermsChanged,
              ),
              Expanded(
                child: Text(
                  'I agree to ForkMesh Terms and Privacy.',
                  style: TextStyle(
                    color: FmTheme.textSecondary(context),
                    fontSize: 14,
                  ),
                ),
              ),
            ],
          ),
          if (_hint.isNotEmpty) ...[
            const SizedBox(height: 14),
            _AuthHint(text: _hint, bad: _bad),
          ],
          const SizedBox(height: 18),
          _PrimaryAuthButton(
            label: _loading ? 'Creating...' : 'Create account',
            onPressed: _loading ? null : _submit,
          ),
          const SizedBox(height: 18),
          _SecondaryAuthButton(label: 'Back', onPressed: widget.onLogin),
          const SizedBox(height: 72),
          const _TermsCopy(),
        ],
      ),
    );
  }
}

class _AuthPageScaffold extends StatelessWidget {
  const _AuthPageScaffold({
    required this.pageTitle,
    required this.onBack,
    required this.child,
  });

  final String pageTitle;
  final VoidCallback onBack;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: FmTheme.bgBase(context),
      body: Column(
        children: [
          Material(
            color: FmTheme.bgRaised(context),
            child: SafeArea(
              bottom: false,
              child: FmPanelHeader(
                title: pageTitle,
                leading: IconButton(
                  onPressed: onBack,
                  icon: const Icon(Icons.chevron_left, size: 30),
                  color: FmTheme.textPrimary(context),
                  tooltip: 'Back',
                ),
              ),
            ),
          ),
          Expanded(
            child: SafeArea(
              top: false,
              child: LayoutBuilder(
                builder: (context, constraints) {
                  return SingleChildScrollView(
                    child: ConstrainedBox(
                      constraints: BoxConstraints(
                        minHeight: constraints.maxHeight,
                      ),
                      child: Padding(
                        padding: const EdgeInsets.fromLTRB(
                          FmSpace.x4,
                          FmSpace.x4,
                          FmSpace.x4,
                          FmSpace.x5,
                        ),
                        child: child,
                      ),
                    ),
                  );
                },
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _AuthContentColumn extends StatelessWidget {
  const _AuthContentColumn({required this.children});

  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 520),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: children,
        ),
      ),
    );
  }
}

class _CenteredLogo extends StatelessWidget {
  const _CenteredLogo({required this.size});

  final double size;

  @override
  Widget build(BuildContext context) {
    return Center(child: _LogoImage(size: size, shadow: true));
  }
}

class _LogoImage extends StatelessWidget {
  const _LogoImage({required this.size, this.shadow = false});

  final double size;
  final bool shadow;

  @override
  Widget build(BuildContext context) {
    return Container(
      key: const ValueKey('forkmesh-auth-logo'),
      width: size,
      height: size,
      decoration: BoxDecoration(
        color: Colors.black,
        borderRadius: BorderRadius.circular(size * 0.24),
        boxShadow: shadow
            ? const [
                BoxShadow(
                  color: Color(0x1A000000),
                  blurRadius: 18,
                  offset: Offset(0, 8),
                ),
              ]
            : null,
      ),
      clipBehavior: Clip.antiAlias,
      child: Image.asset(
        _logoAsset,
        fit: BoxFit.cover,
        filterQuality: FilterQuality.medium,
      ),
    );
  }
}

class _AuthTitle extends StatelessWidget {
  const _AuthTitle({required this.title, required this.subtitle});

  final String title;
  final String subtitle;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Text(
          title,
          textAlign: TextAlign.center,
          style: TextStyle(
            color: FmTheme.textPrimary(context),
            fontSize: 28,
            height: 1.12,
            fontWeight: FontWeight.w800,
            letterSpacing: 0,
          ),
        ),
        const SizedBox(height: 10),
        Text(
          subtitle,
          textAlign: TextAlign.center,
          style: TextStyle(
            color: FmTheme.textSecondary(context),
            fontSize: 17,
            height: 1.25,
            fontWeight: FontWeight.w500,
          ),
        ),
      ],
    );
  }
}

class _AuthField extends StatelessWidget {
  const _AuthField({
    required this.label,
    required this.controller,
    this.hintText = '',
    this.obscureText = false,
    this.keyboardType,
    this.textInputAction,
    this.onSubmitted,
    this.onChanged,
  });

  final String label;
  final TextEditingController controller;
  final String hintText;
  final bool obscureText;
  final TextInputType? keyboardType;
  final TextInputAction? textInputAction;
  final ValueChanged<String>? onSubmitted;
  final ValueChanged<String>? onChanged;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          label,
          style: TextStyle(
            color: FmTheme.textPrimary(context),
            fontSize: 16,
            fontWeight: FontWeight.w600,
          ),
        ),
        const SizedBox(height: FmSpace.x2),
        TextField(
          controller: controller,
          obscureText: obscureText,
          keyboardType: keyboardType,
          textInputAction: textInputAction,
          onSubmitted: onSubmitted,
          onChanged: onChanged,
          style: TextStyle(
            color: FmTheme.textPrimary(context),
            fontSize: 16,
            fontWeight: FontWeight.w500,
          ),
          decoration: InputDecoration(
            hintText: hintText,
            filled: true,
            fillColor: FmTheme.bgRaised(context),
            contentPadding: const EdgeInsets.symmetric(
              horizontal: FmSpace.x4,
              vertical: FmSpace.x4,
            ),
            enabledBorder: OutlineInputBorder(
              borderRadius: BorderRadius.circular(FmRadius.md),
              borderSide: BorderSide.none,
            ),
            focusedBorder: OutlineInputBorder(
              borderRadius: BorderRadius.circular(FmRadius.md),
              borderSide: BorderSide(
                color: FmTheme.accent(context),
                width: 1.2,
              ),
            ),
          ),
        ),
      ],
    );
  }
}

class _AuthHint extends StatelessWidget {
  const _AuthHint({required this.text, this.bad = false});

  final String text;
  final bool bad;

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: TextStyle(
        color: bad ? FmTheme.danger(context) : FmTheme.success(context),
        fontSize: 13,
        height: 1.35,
        fontWeight: FontWeight.w600,
      ),
    );
  }
}

class _PrimaryAuthButton extends StatelessWidget {
  const _PrimaryAuthButton({required this.label, required this.onPressed});

  final String label;
  final VoidCallback? onPressed;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      key: const ValueKey('primary-auth-button-frame'),
      height: 48,
      child: FilledButton(
        onPressed: onPressed,
        style: FilledButton.styleFrom(
          backgroundColor: FmTheme.textPrimary(context),
          foregroundColor: FmTheme.isDark(context)
              ? FmColors.darkBgBase
              : FmColors.bgRaised,
          elevation: 0,
          minimumSize: const Size.fromHeight(48),
          padding: const EdgeInsets.symmetric(vertical: FmSpace.x3),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(FmRadius.md),
          ),
          textStyle: const TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
        ),
        child: Text(label),
      ),
    );
  }
}

class _SecondaryAuthButton extends StatelessWidget {
  const _SecondaryAuthButton({required this.label, required this.onPressed});

  final String label;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return TextButton(
      onPressed: onPressed,
      style: TextButton.styleFrom(
        foregroundColor: FmTheme.textPrimary(context),
        textStyle: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
      ),
      child: Text(label),
    );
  }
}

class _TermsCopy extends StatelessWidget {
  const _TermsCopy();

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(10, 24, 10, 0),
      child: Text.rich(
        const TextSpan(
          text: 'By continuing, you agree to our ',
          children: [
            TextSpan(
              text: 'Terms of Service',
              style: TextStyle(decoration: TextDecoration.underline),
            ),
            TextSpan(text: ' and have read our '),
            TextSpan(
              text: 'Privacy Policy',
              style: TextStyle(decoration: TextDecoration.underline),
            ),
            TextSpan(text: '.'),
          ],
        ),
        textAlign: TextAlign.left,
        style: TextStyle(
          color: FmTheme.textSecondary(context),
          fontSize: 14,
          height: 1.45,
        ),
      ),
    );
  }
}
