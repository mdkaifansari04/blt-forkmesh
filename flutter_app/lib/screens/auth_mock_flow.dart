import 'package:flutter/material.dart';

const _logoAsset = 'assets/images/logo.png';
const _authBackground = Color(0xFFFBFAF9);
const _authTopBar = Color(0xFFF0EFF1);
const _authText = Color(0xFF2D2C2A);
const _authMuted = Color(0xFF8F8D89);
const _authButton = Color(0xFF4B4948);

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

class _LoginScreen extends StatelessWidget {
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
  Widget build(BuildContext context) {
    return _AuthPageScaffold(
      pageTitle: 'Login',
      onBack: onBack,
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
          const _AuthField(
            label: 'Email',
            initialText: 'alexsmith.mirror@forkmesh.dev',
          ),
          const SizedBox(height: 24),
          const _AuthField(
            label: 'Password',
            initialText: '••••••••••••••',
            trailing: Icon(
              Icons.visibility_off_outlined,
              color: _authMuted,
              size: 20,
            ),
          ),
          const SizedBox(height: 42),
          _PrimaryAuthButton(label: 'Continue', onPressed: onAuthenticated),
          const SizedBox(height: 18),
          _SecondaryAuthButton(label: 'Create an account', onPressed: onSignup),
          const SizedBox(height: 72),
          const _TermsCopy(),
        ],
      ),
    );
  }
}

class _SignupScreen extends StatelessWidget {
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
  Widget build(BuildContext context) {
    return _AuthPageScaffold(
      pageTitle: 'Sign up',
      onBack: onBack,
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
          const _AuthField(
            label: 'Public node name',
            initialText: 'ada-lovelace',
          ),
          const SizedBox(height: 20),
          const _AuthField(
            label: 'Email',
            initialText: 'alexsmith.mirror@forkmesh.dev',
            trailing: Text(
              'Edit',
              style: TextStyle(
                color: Color(0xFF1B84A6),
                fontSize: 16,
                fontWeight: FontWeight.w700,
              ),
            ),
          ),
          const SizedBox(height: 20),
          const _AuthField(
            label: 'Password',
            initialText: '••••••••••••••',
            trailing: Icon(
              Icons.visibility_off_outlined,
              color: _authMuted,
              size: 20,
            ),
          ),
          const SizedBox(height: 22),
          Row(
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              Checkbox(value: acceptedTerms, onChanged: onTermsChanged),
              const Expanded(
                child: Text(
                  'I agree to ForkMesh Terms and Privacy.',
                  style: TextStyle(color: _authMuted, fontSize: 14),
                ),
              ),
            ],
          ),
          const SizedBox(height: 18),
          _PrimaryAuthButton(
            label: 'Create account',
            onPressed: onAuthenticated,
          ),
          const SizedBox(height: 18),
          _SecondaryAuthButton(label: 'Back', onPressed: onLogin),
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
      backgroundColor: _authBackground,
      appBar: AppBar(
        toolbarHeight: 62,
        backgroundColor: _authTopBar,
        foregroundColor: _authText,
        surfaceTintColor: Colors.transparent,
        elevation: 0,
        centerTitle: true,
        leading: IconButton(
          onPressed: onBack,
          icon: const Icon(Icons.chevron_left, size: 30),
          tooltip: 'Back',
        ),
        title: Text(
          pageTitle,
          style: const TextStyle(
            color: _authText,
            fontSize: 18,
            fontWeight: FontWeight.w800,
          ),
        ),
      ),
      body: SafeArea(
        top: false,
        child: LayoutBuilder(
          builder: (context, constraints) {
            return SingleChildScrollView(
              child: ConstrainedBox(
                constraints: BoxConstraints(minHeight: constraints.maxHeight),
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(14, 18, 14, 28),
                  child: child,
                ),
              ),
            );
          },
        ),
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
          style: const TextStyle(
            color: _authText,
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
          style: const TextStyle(
            color: _authMuted,
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
    required this.initialText,
    this.trailing,
  });

  final String label;
  final String initialText;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          label,
          style: const TextStyle(
            color: _authText,
            fontSize: 16,
            fontWeight: FontWeight.w600,
          ),
        ),
        const SizedBox(height: 10),
        Container(
          constraints: const BoxConstraints(minHeight: 54),
          decoration: BoxDecoration(
            color: Colors.white,
            borderRadius: BorderRadius.circular(7),
            boxShadow: const [
              BoxShadow(
                color: Color(0x05000000),
                blurRadius: 2,
                offset: Offset(0, 1),
              ),
            ],
          ),
          child: Row(
            children: [
              Expanded(
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  child: Text(
                    initialText,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      color: _authText,
                      fontSize: 16,
                      fontWeight: FontWeight.w500,
                    ),
                  ),
                ),
              ),
              if (trailing != null) ...[
                Padding(
                  padding: const EdgeInsets.only(right: 14),
                  child: trailing,
                ),
              ],
            ],
          ),
        ),
      ],
    );
  }
}

class _PrimaryAuthButton extends StatelessWidget {
  const _PrimaryAuthButton({required this.label, required this.onPressed});

  final String label;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      key: const ValueKey('primary-auth-button-frame'),
      height: 48,
      child: FilledButton(
        onPressed: onPressed,
        style: FilledButton.styleFrom(
          backgroundColor: _authButton,
          foregroundColor: Colors.white,
          elevation: 0,
          minimumSize: const Size.fromHeight(48),
          padding: const EdgeInsets.symmetric(vertical: 12),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(12),
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
        foregroundColor: _authText,
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
    return const Padding(
      padding: EdgeInsets.fromLTRB(10, 24, 10, 0),
      child: Text.rich(
        TextSpan(
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
        style: TextStyle(color: _authMuted, fontSize: 14, height: 1.45),
      ),
    );
  }
}
