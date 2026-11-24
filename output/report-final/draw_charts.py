from pathlib import Path
import sys,json
sys.path.insert(0,str(Path('bin/report17-deps').resolve()))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties
from matplotlib.ticker import FuncFormatter
root=Path.cwd();out=root/'output/report-final/figures';font=FontProperties(fname='C:/Windows/Fonts/msyh.ttc')
plt.rcParams.update({'font.family':font.get_name(),'axes.unicode_minus':False,'font.size':13,'axes.spines.top':False,'axes.spines.right':False,'axes.spines.left':False,'axes.edgecolor':'#bac6d4','text.color':'#193858','axes.labelcolor':'#193858','xtick.color':'#4d637c','ytick.color':'#193858','savefig.facecolor':'white'})
old='#b9cbd8';new='#237f91'
sm3=json.loads((root/'results/sm3-mcu-2026-11-24/board.json').read_text(encoding='utf-8'))
sq=json.loads((root/'results/sm2-square-2026-11-24/board.json').read_text(encoding='utf-8'))
fig,axs=plt.subplots(1,2,figsize=(13,5.4));fig.subplots_adjust(left=.09,right=.96,top=.82,bottom=.24,wspace=.35)
a=[76482,290850,38145];b=[49112,186200,25265]
for ax,labels,ratios in [(axs[0],['SM3 1 KB','SM3 4 KB','HMAC 256 B'],[x/y for x,y in zip(a,b)]),(axs[1],['域平方','域求逆','导出公钥','签名','验签','ECDH'],[x/y for x,y in zip(sq['before'][:6],sq['after'][:6])])]:
 bars=ax.barh(labels[::-1],ratios[::-1],color=new,height=.56);ax.set_xlim(0,1.85 if ax==axs[0] else 1.25);ax.set_xlabel('优化前周期 ÷ 优化后周期');ax.grid(axis='x',alpha=.18);ax.set_axisbelow(True)
 for bar,val in zip(bars,ratios[::-1]):ax.text(val+.025,bar.get_y()+bar.get_height()/2,f'{val:.3f}×',va='center',fontsize=12)
axs[0].set_title('SM3 滚动内核',fontproperties=font,fontsize=17,pad=22);axs[1].set_title('SM2 专用平方及相关操作',fontproperties=font,fontsize=17,pad=22)
fig.text(.5,.06,'F103 · 同固件 A/B；SM3 对照上一版 MCU，SM2 对照专用平方接入前 OPT',ha='center',fontproperties=font,fontsize=11)
fig.savefig(out/'fig3-1.png',dpi=200,bbox_inches='tight',pad_inches=.18);plt.close(fig)
fig,axs=plt.subplots(1,2,figsize=(13,5.4));fig.subplots_adjust(left=.07,right=.95,top=.8,bottom=.2,wspace=.4)
ax=axs[0];values=[30444,2324];ax.barh(['当前客户端'],[30444],color=new,height=.42);ax.barh(['当前客户端'],[2324],left=[30444],color='#e3eaf0',height=.42);ax.set_xlim(0,32768);ax.set_xlabel('Flash 加载量 / B');ax.set_title('32 KiB Flash 链接通过',fontproperties=font,fontsize=17,pad=25);ax.text(15222,0,'30,444 B',va='center',ha='center',color='white',fontsize=17);ax.text(16384,-.4,'剩余 2,324 B',ha='center',fontsize=14);ax.set_ylim(-.6,.6)
ax=axs[1];labels=['协议上下文','库静态区','最长栈路径'];vals=[5256,4104,4952];bars=ax.barh(labels[::-1],vals[::-1],color=['#237f91','#6f98af','#b5cbd8']);ax.set_xlim(0,6600);ax.set_xlabel('协议预算组成 / B');ax.set_title('20 KiB RAM 预算门禁',fontproperties=font,fontsize=17,pad=25)
for bar,val in zip(bars,vals[::-1]):ax.text(val+110,bar.get_y()+bar.get_height()/2,f'{val:,} B',va='center',fontsize=13)
fig.text(.5,.065,'整机静态 RAM：9,528 B；协议预算下界：14,312 B（两种构建口径分别核算）',ha='center',fontproperties=font,fontsize=12)
fig.savefig(out/'fig3-5.png',dpi=200,bbox_inches='tight',pad_inches=.18);plt.close(fig)
fig,axs=plt.subplots(1,2,figsize=(13,5.4));fig.subplots_adjust(left=.1,right=.97,top=.81,bottom=.23,wspace=.4)
for ax,labels,vals,title in [(axs[0],['动态身份','只读预置'],[9652,5182],'平台初始化后到首条 ACK'),(axs[1],['FULL','RESUME'],[4467.285,50.536],'网关首包到握手完成')]:
 bars=ax.bar(labels,vals,color=[old,new],width=.5);ax.set_ylabel('时间 / ms');ax.set_title(title,fontproperties=font,fontsize=17,pad=25);ax.set_ylim(0,max(vals)*1.23);ax.grid(axis='y',alpha=.16);ax.set_axisbelow(True)
 for bar,val in zip(bars,vals):ax.text(bar.get_x()+bar.get_width()/2,val+max(vals)*.025,f'{val:,.0f}' if val>9000 or val==5182 else f'{val:,.3f}',ha='center',fontsize=15)
fig.text(.29,.06,'3 组交错板测中位数；减少 46.31%',ha='center',fontproperties=font,fontsize=11);fig.text(.79,.06,'单轮同拓扑观测；握手字节 1198 → 428 B',ha='center',fontproperties=font,fontsize=11)
fig.savefig(out/'fig3-2.png',dpi=200,bbox_inches='tight',pad_inches=.18);plt.close(fig)
print('Rendered 3 exact-data charts')

