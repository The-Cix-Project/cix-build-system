# ADR-0018: Container recipes and CPDL

Container recipes remain cixd-owned composition declarations. CPDL describes
package builds only; a container recipe may reference verified CIXPKG identities
but cannot embed build phases or shell commands.
