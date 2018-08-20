JedecChain;
	FileRevision(JESD32A);
	DefaultMfr(6E);

	P ActionCode(Cfg)
		/* Device PartName(EP4CGX22) Path("/root/netup_testbench-transcoder/12.0sp2/quartus/bin/quartus_pgm/") File("netup_unidvb_top.jic") MfrSpec(OpMask(1) SEC_Device(EPCS128) Child_OpMask(1 7)); */
		Device PartName(EP4CE22) Path("../fw/fw-0.37/") File("joker_tv-0.37.jic") MfrSpec(OpMask(1) SEC_Device(EPCS128) Child_OpMask(1 7));

ChainEnd;

AlteraBegin;
	ChainType(JTAG);
AlteraEnd;
