/** PocketJS adapter: only the upstream default silhouette is used (shape=null).
 * Upstream returns NUL for this case; skip its custom-shape calibration at boot.
 * Original solver is retained as eyefit.upstream.ts, pinned in SOURCE.md.
 */
export function decalageDesYeux(radii:number[]|null,_state:string,_expression:string|null){
 if(radii!==null)throw new Error('Workshop avatar supports the default bloub shape only');
 return {x:0,y:0};
}
